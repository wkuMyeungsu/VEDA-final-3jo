/*
 * VL53L1X ToF 거리 센서 캐릭터 디바이스 드라이버
 *
 * 뼈대: 05_dev.c(BMP280) - cdev/class/device_create + file_operations
 * 통신: 02~04_dev.c(OLED) - raw i2c_master_send 패턴
 *       단, VL53L1X는 레지스터 주소가 16비트라 i2c_smbus_* 계열
 *       (8비트 command byte 전제)은 쓸 수 없어서 직접 프레이밍한다.
 *
 * 초기화 시퀀스(91바이트, 레지스터 0x002D~0x0087)와 측정 흐름은
 * Adafruit CircuitPython VL53L1X 드라이버(MIT License)를 참고해
 * 커널 C로 이식했다. 원본: adafruit_vl53l1x.py, __repo__ =
 * https://github.com/adafruit/Adafruit_CircuitPython_VL53L1X.git
 *
 * 대상: Raspberry Pi 4B, kernel 6.1.21-v8+, I2C bus 1, addr 0x29
 *
 * 빌드 전 확인:
 *   i2cdetect -y 1     # 0x29에 디바이스 잡히는지 확인
 *
 * 빌드/실행 (forklift-device/sensors/kernel/Makefile, 표준 Kbuild out-of-tree 모듈):
 *   make                       # vl53l1x_driver.ko 빌드
 *   sudo make install          # insmod
 *   cat /dev/vl53l1x0          # 측정값(mm) 출력
 *   sudo make uninstall        # rmmod
 *   make clean
 *
 * 유저스페이스(forklift-device/sensors)와는 빌드 시스템이 완전히 분리되어
 * 있음 -> 유저스페이스는 CMake, 이쪽은 Kbuild. 유저스페이스에서는
 * VL53L1XKernelDriver(sensors/vl53l1x_kernel_driver.hpp)가 이 드라이버가
 * 만든 /dev/vl53l1x0을 읽는다.
 *
 * TODO (다음 단계):
 *   - device tree 오버레이로 전환 (지금은 board_info 수동 등록)
 *   - 필요시 timing_budget 가변 설정을 ioctl로 노출
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/string.h>

#define DRIVER_NAME  "vl53l1x0"
#define DRIVER_CLASS "vl53l1xClass"

#define I2C_BUS_AVAILABLE     1        /* Raspberry Pi 4 기본 I2C 버스 */
#define VL53L1X_SLAVE_ADDR    0x29     /* = 41 decimal, 실측 배선 주소 */

/* ---- 레지스터 주소 (16비트, 빅엔디안으로 전송) ---- */
#define REG_VHV_CFG_TIMEOUT_LOOP_BOUND   0x0008
#define REG_RANGE_TIMEOUT_MACROP_A_HI    0x005E
#define REG_RANGE_TIMEOUT_MACROP_B_HI    0x0061
#define REG_SYSTEM_INTERRUPT_CLEAR       0x0086
#define REG_SYSTEM_MODE_START            0x0087
#define REG_RESULT_RANGE_STATUS          0x0089
#define REG_RESULT_FINAL_RANGE_MM        0x0096
#define REG_IDENTIFICATION_MODEL_ID      0x010F
#define REG_GPIO_TIO_HV_STATUS           0x0031
#define REG_MISC_LOOP_BOUND_0B           0x000B

#define INIT_SEQ_START_ADDR              0x002D
#define INIT_SEQ_LEN                     91

#define RANGE_STATUS_VALID               0x09

/* VL53L1X 부트 초기화 시퀀스 (레지스터 0x2D부터 순서대로) */
static const u8 vl53l1x_init_seq[INIT_SEQ_LEN] = {
	0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08, /* 0x2D-0x34 */
	0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00, /* 0x35-0x3C */
	0x00, 0xFF, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, /* 0x3D-0x44 */
	0x00, 0x20, 0x0B, 0x00, 0x00, 0x02, 0x0A, 0x21, /* 0x45-0x4C */
	0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC8, /* 0x4D-0x54 */
	0x00, 0x00, 0x38, 0xFF, 0x01, 0x00, 0x08, 0x00, /* 0x55-0x5C */
	0x00, 0x01, 0xCC, 0x0F, 0x01, 0xF1, 0x0D, 0x01, /* 0x5D-0x64 */
	0x68, 0x00, 0x80, 0x08, 0xB8, 0x00, 0x00, 0x00, /* 0x65-0x6C */
	0x00, 0x0F, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x6D-0x74 */
	0x00, 0x00, 0x01, 0x0F, 0x0D, 0x0E, 0x0E, 0x00, /* 0x75-0x7C */
	0x00, 0x02, 0xC7, 0xFF, 0x9B, 0x00, 0x00, 0x00, /* 0x7D-0x84 */
	0x01, 0x00, 0x00,                                /* 0x85-0x87 */
};

/*
 * 주의: 위 표에서 0x5E(RANGE_CONFIG_TIMEOUT_MACROP_A_HI)는
 * 초기화 시퀀스가 끝난 뒤 timing_budget 적용 단계에서
 * 0x01CC로 한 번 더 덮어써진다 (아래 vl53l1x_apply_timing_budget 참고).
 * init_seq의 값은 부트 시점 기본값일 뿐이라 최종값과 다를 수 있다.
 */

static struct i2c_client *vl53l1x_client;
static dev_t vl53l1x_devno;
static struct class *vl53l1x_class;
static struct cdev vl53l1x_cdev;

/* /dev/vl53l1x0 노드를 0666으로 생성해 sudo 없이 읽을 수 있게 한다.
 * 커널 6.2부터 devnode 콜백 시그니처가 const struct device *로 바뀌었고,
 * 이 드라이버의 빌드 대상(6.18.29+rpt-rpi-v8)도 해당 시그니처를 쓴다
 * (/lib/modules/$(uname -r)/build/include/linux/device/class.h 확인).
 */
static char *vl53l1x_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return NULL;
}

/* ---- 저수준 I2C 접근 ---- */

/* reg(16bit) + data[len] 를 한 트랜잭션으로 write */
static int vl53l1x_write(struct i2c_client *client, u16 reg,
			  const u8 *data, size_t len)
{
	u8 *buf;
	int ret;

	buf = kmalloc(len + 2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = (reg >> 8) & 0xFF;
	buf[1] = reg & 0xFF;
	memcpy(&buf[2], data, len);

	ret = i2c_master_send(client, buf, len + 2);
	kfree(buf);

	if (ret != (int)(len + 2)) {
		pr_err("vl53l1x: write reg 0x%04x failed (ret=%d)\n", reg, ret);
		return (ret < 0) ? ret : -EIO;
	}
	return 0;
}

static inline int vl53l1x_write8(struct i2c_client *client, u16 reg, u8 val)
{
	return vl53l1x_write(client, reg, &val, 1);
}

static inline int vl53l1x_write16(struct i2c_client *client, u16 reg, u16 val)
{
	u8 buf[2] = { (val >> 8) & 0xFF, val & 0xFF };

	return vl53l1x_write(client, reg, buf, 2);
}

/* reg(16bit) 주소를 먼저 쓰고, repeated start로 len바이트 read */
static int vl53l1x_read(struct i2c_client *client, u16 reg, u8 *buf, size_t len)
{
	u8 addr_buf[2] = { (reg >> 8) & 0xFF, reg & 0xFF };
	struct i2c_msg msgs[2] = {
		{
			.addr  = client->addr,
			.flags = 0,
			.len   = 2,
			.buf   = addr_buf,
		},
		{
			.addr  = client->addr,
			.flags = I2C_M_RD,
			.len   = len,
			.buf   = buf,
		},
	};
	int ret = i2c_transfer(client->adapter, msgs, 2);

	if (ret != 2) {
		pr_err("vl53l1x: read reg 0x%04x failed (ret=%d)\n", reg, ret);
		return (ret < 0) ? ret : -EIO;
	}
	return 0;
}

static inline int vl53l1x_read8(struct i2c_client *client, u16 reg, u8 *val)
{
	return vl53l1x_read(client, reg, val, 1);
}

/* ---- 측정 로직 (adafruit_vl53l1x.py 이식) ---- */

static int vl53l1x_start_ranging(struct i2c_client *client)
{
	return vl53l1x_write8(client, REG_SYSTEM_MODE_START, 0x40);
}

static int vl53l1x_stop_ranging(struct i2c_client *client)
{
	return vl53l1x_write8(client, REG_SYSTEM_MODE_START, 0x00);
}

static int vl53l1x_clear_interrupt(struct i2c_client *client)
{
	return vl53l1x_write8(client, REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
}

/* data_ready 폴링. 최대 약 120ms 대기 후 타임아웃 */
static int vl53l1x_wait_data_ready(struct i2c_client *client)
{
	int i, ret;
	u8 status;

	for (i = 0; i < 60; i++) {
		ret = vl53l1x_read8(client, REG_GPIO_TIO_HV_STATUS, &status);
		if (ret)
			return ret;

		/* init_seq가 0x30=0x01(active-high 극성)로 고정 설정하므로
		 * bit0 == 1 이면 데이터 준비된 것으로 판단한다.
		 */
		if (status & 0x01)
			return 0;

		msleep(2);
	}
	pr_warn("vl53l1x: data_ready timeout\n");
	return -ETIMEDOUT;
}

/* timing_budget 50ms / long-distance 모드 고정 적용
 * (adafruit 드라이버 __init__ 기본값과 동일하게 맞춰,
 *  기존에 검증해 온 유저스페이스 드라이버 동작과 일치시킨다)
 */
static int vl53l1x_apply_timing_budget(struct i2c_client *client)
{
	int ret;

	ret = vl53l1x_write16(client, REG_RANGE_TIMEOUT_MACROP_A_HI, 0x01CC);
	if (ret)
		return ret;
	return vl53l1x_write16(client, REG_RANGE_TIMEOUT_MACROP_B_HI, 0x01EA);
}

static int vl53l1x_sensor_init(struct i2c_client *client)
{
	int ret;
	u8 model_id[3];

	/* 센서 식별 확인 (Model ID=0xEA, Module Type=0xCC, Mask Rev=0x10) */
	ret = vl53l1x_read(client, REG_IDENTIFICATION_MODEL_ID, model_id, 3);
	if (ret)
		return ret;

	if (model_id[0] != 0xEA || model_id[1] != 0xCC || model_id[2] != 0x10) {
		pr_err("vl53l1x: unexpected sensor ID %02x %02x %02x (expected EA CC 10)\n",
		       model_id[0], model_id[1], model_id[2]);
		return -ENODEV;
	}

	/* 91바이트 부트 시퀀스를 한 번에 write */
	ret = vl53l1x_write(client, INIT_SEQ_START_ADDR, vl53l1x_init_seq, INIT_SEQ_LEN);
	if (ret)
		return ret;

	/* 첫 측정 1회 수행 후 정지 (부트 시퀀스 마무리 절차) */
	ret = vl53l1x_start_ranging(client);
	if (ret)
		return ret;

	ret = vl53l1x_wait_data_ready(client);
	if (ret) {
		vl53l1x_stop_ranging(client);
		return ret;
	}

	ret = vl53l1x_clear_interrupt(client);
	if (ret)
		return ret;

	ret = vl53l1x_stop_ranging(client);
	if (ret)
		return ret;

	ret = vl53l1x_write8(client, REG_VHV_CFG_TIMEOUT_LOOP_BOUND, 0x09);
	if (ret)
		return ret;

	ret = vl53l1x_write8(client, REG_MISC_LOOP_BOUND_0B, 0x00);
	if (ret)
		return ret;

	return vl53l1x_apply_timing_budget(client);
}

/* 단발 측정 1회 수행. 성공 시 mm 단위 거리를 반환(>=0),
 * 유효 판정 실패(가림/범위밖 등)면 -1을 반환한다 (에러 아님, "무효"),
 * I2C 통신 실패 등 진짜 에러는 음의 errno를 반환한다.
 */
static int vl53l1x_measure_mm(struct i2c_client *client)
{
	int ret;
	u8 status;
	u8 range_buf[2];
	u16 range_mm;

	ret = vl53l1x_start_ranging(client);
	if (ret)
		return ret;

	ret = vl53l1x_wait_data_ready(client);
	if (ret) {
		vl53l1x_stop_ranging(client);
		return ret;
	}

	ret = vl53l1x_read8(client, REG_RESULT_RANGE_STATUS, &status);
	if (ret)
		goto out_stop;

	if (status != RANGE_STATUS_VALID) {
		ret = -1; /* 유효하지 않은 측정 (에러 아님) */
		goto out_clear;
	}

	ret = vl53l1x_read(client, REG_RESULT_FINAL_RANGE_MM, range_buf, 2);
	if (ret)
		goto out_clear;

	range_mm = ((u16)range_buf[0] << 8) | range_buf[1];
	ret = (int)range_mm;

out_clear:
	vl53l1x_clear_interrupt(client);
out_stop:
	vl53l1x_stop_ranging(client);
	return ret;
}

/* ---- 캐릭터 디바이스 인터페이스 ---- */

static ssize_t driver_read(struct file *file, char __user *user_buf,
			    size_t count, loff_t *offs)
{
	char out_string[16];
	int len, to_copy;
	int mm;

	if (*offs > 0)
		return 0; /* 매 open마다 한 줄만: cat 사용 편의 */

	mm = vl53l1x_measure_mm(vl53l1x_client);

	if (mm >= 0)
		len = scnprintf(out_string, sizeof(out_string), "%d\n", mm);
	else if (mm == -1)
		len = scnprintf(out_string, sizeof(out_string), "-1\n"); /* 무효 측정 */
	else
		return mm; /* 실제 I2C 에러 errno 그대로 전달 */

	to_copy = min((int)count, len);
	if (copy_to_user(user_buf, out_string, to_copy))
		return -EFAULT;

	*offs += to_copy;
	return to_copy;
}

static int driver_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int driver_close(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations fops = {
	.owner   = THIS_MODULE,
	.open    = driver_open,
	.read    = driver_read,
	.release = driver_close,
};

/* ---- i2c_driver / probe / remove ---- */

static int vl53l1x_probe(struct i2c_client *client)
{
	int ret;

	vl53l1x_client = client;

	ret = vl53l1x_sensor_init(client);
	if (ret) {
		pr_err("vl53l1x: sensor init failed (%d)\n", ret);
		return ret;
	}

	pr_info("vl53l1x: probed and initialized OK\n");
	return 0;
}

static void vl53l1x_remove(struct i2c_client *client)
{
	pr_info("vl53l1x: remove\n");
}

static const struct i2c_device_id vl53l1x_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vl53l1x_id);

static struct i2c_driver vl53l1x_driver = {
	.driver = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe  = vl53l1x_probe,
	.remove = vl53l1x_remove,
	.id_table = vl53l1x_id,
};

static struct i2c_board_info vl53l1x_board_info = {
	I2C_BOARD_INFO(DRIVER_NAME, VL53L1X_SLAVE_ADDR)
};

static struct i2c_adapter *vl53l1x_adapter;
static struct i2c_client  *vl53l1x_bound_client;

static int __init mod_init(void)
{
	int ret = -1;

	if (alloc_chrdev_region(&vl53l1x_devno, 0, 1, DRIVER_NAME) < 0) {
		pr_err("vl53l1x: chrdev region alloc failed\n");
		return -1;
	}

	vl53l1x_class = class_create(DRIVER_CLASS);
	if (!vl53l1x_class) {
		pr_err("vl53l1x: class create failed\n");
		goto err_chrdev;
	}
	vl53l1x_class->devnode = vl53l1x_devnode;

	if (!device_create(vl53l1x_class, NULL, vl53l1x_devno, NULL, DRIVER_NAME)) {
		pr_err("vl53l1x: device create failed\n");
		goto err_class;
	}

	cdev_init(&vl53l1x_cdev, &fops);
	if (cdev_add(&vl53l1x_cdev, vl53l1x_devno, 1) == -1) {
		pr_err("vl53l1x: cdev add failed\n");
		goto err_device;
	}

	vl53l1x_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);
	if (!vl53l1x_adapter) {
		pr_err("vl53l1x: i2c_get_adapter(%d) failed\n", I2C_BUS_AVAILABLE);
		goto err_cdev;
	}

	vl53l1x_bound_client = i2c_new_client_device(vl53l1x_adapter, &vl53l1x_board_info);
	if (!vl53l1x_bound_client) {
		pr_err("vl53l1x: i2c_new_client_device failed\n");
		i2c_put_adapter(vl53l1x_adapter);
		goto err_cdev;
	}

	if (i2c_add_driver(&vl53l1x_driver) != 0) {
		pr_err("vl53l1x: i2c_add_driver failed\n");
		i2c_unregister_device(vl53l1x_bound_client);
		i2c_put_adapter(vl53l1x_adapter);
		goto err_cdev;
	}

	pr_info("vl53l1x: driver loaded, node: /dev/%s\n", DRIVER_NAME);
	return 0;

err_cdev:
	cdev_del(&vl53l1x_cdev);
err_device:
	device_destroy(vl53l1x_class, vl53l1x_devno);
err_class:
	class_destroy(vl53l1x_class);
err_chrdev:
	unregister_chrdev_region(vl53l1x_devno, 1);
	return ret;
}

static void __exit mod_exit(void)
{
	i2c_unregister_device(vl53l1x_bound_client);
	i2c_del_driver(&vl53l1x_driver);

	cdev_del(&vl53l1x_cdev);
	device_destroy(vl53l1x_class, vl53l1x_devno);
	class_destroy(vl53l1x_class);
	unregister_chrdev_region(vl53l1x_devno, 1);

	pr_info("vl53l1x: driver unloaded\n");
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("veda8-team3");
MODULE_DESCRIPTION("VL53L1X ToF distance sensor character device driver");
