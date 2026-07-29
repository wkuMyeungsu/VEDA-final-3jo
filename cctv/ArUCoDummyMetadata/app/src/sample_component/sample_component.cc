#include "sample_component.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>

#include "dispatcher_serialize.h"
#include "i_app_dispatcher.h"
#include "i_metadata_manager.h"
#include "i_p_metadata_manager.h"
#include "metadata_format.h"

namespace {
// 실물 마커 없이 수신측(메타데이터 소비하는 쪽) 테스트를 위한 가짜 검출 결과 생성.
// DICT_4X4_50 범위(0~49)에서 서로 다른 id를 뽑고, 화면 안에 그럴듯한 사각형 코너를 흩뿌림.
// (ArUco_Detection이 실제로 쓰는 딕셔너리/포맷과 맞춰서 수신측이 구분 못 하게 함)
constexpr int kTestFrameWidth = 1920;
constexpr int kTestFrameHeight = 1080;
constexpr int kMaxDictId = 49;

// 랜덤 ArUco 검출 결과를 만들어낸다 (실제 카메라/마커 없이 테스트용).
// 입력: count - 만들고 싶은 마커 개수 (1~50 범위로 자동 clamp됨)
// 출력: out_ids    - 서로 겹치지 않는 마커 id 목록 (0~49 중 count개, 매 호출마다 랜덤)
//       out_corners - 마커별 코너 4점(x,y) 목록. out_ids와 인덱스가 1:1로 대응됨
//       (반환값 없음 - 결과는 두 참조 파라미터에 채워서 돌려줌)
void GenerateRandomMarkers(int count, std::vector<int>& out_ids,
                            std::vector<std::vector<MarkerMetadataFormat::Point2f>>& out_corners) {
  static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> cx_dist(60, kTestFrameWidth - 60);
  std::uniform_int_distribution<int> cy_dist(60, kTestFrameHeight - 60);
  std::uniform_int_distribution<int> half_size_dist(20, 80);
  std::uniform_real_distribution<float> jitter_dist(-5.f, 5.f);

  std::vector<int> pool(kMaxDictId + 1);
  std::iota(pool.begin(), pool.end(), 0);
  std::shuffle(pool.begin(), pool.end(), rng);

  count = std::max(1, std::min(count, (int)pool.size()));
  out_ids.assign(pool.begin(), pool.begin() + count);

  out_corners.clear();
  for (int i = 0; i < count; ++i) {
    float cx = (float)cx_dist(rng);
    float cy = (float)cy_dist(rng);
    float half = (float)half_size_dist(rng);
    out_corners.push_back({
        {cx - half + jitter_dist(rng), cy - half + jitter_dist(rng)},
        {cx + half + jitter_dist(rng), cy - half + jitter_dist(rng)},
        {cx + half + jitter_dist(rng), cy + half + jitter_dist(rng)},
        {cx - half + jitter_dist(rng), cy + half + jitter_dist(rng)},
    });
  }
}
}  // namespace

// 기본 생성자. 프레임워크가 기본 ClassID(_SampleComponent_Id)와 이름("SampleComponent")으로
// 컴포넌트를 만들 때 사용됨 (실제 초기화는 아래 위임 생성자가 담당).
// 입력: 없음 / 출력: 없음(생성자)
SampleComponent::SampleComponent() : SampleComponent(_SampleComponent_Id, "SampleComponent") {}

// 실제 생성자. Component 베이스 클래스에 id/name을 넘겨 초기화한다.
// 입력: id - 이 컴포넌트의 ClassID, name - 컴포넌트 인스턴스 이름 문자열
// 출력: 없음(생성자)
SampleComponent::SampleComponent(ClassID id, const char* name) : Component(id, name) {}

// 소멸자. 이 앱은 폴링 스레드 등 별도로 정리할 리소스가 없어 비어있음.
// 입력: 없음 / 출력: 없음
SampleComponent::~SampleComponent() {}

// 컴포넌트 초기화. 프레임워크가 앱을 띄울 때 한 번 호출됨.
// 입력: 없음
// 출력: bool - 초기화 성공 여부 (Component::Initialize()의 결과를 그대로 반환)
// 동작: "/testsend" URI를 등록만 하고 끝남. 실제 카메라를 보지 않으므로 aruco_detection과
//       달리 폴링 스레드는 없음.
bool SampleComponent::Initialize() {
  RegisterURI();
  return Component::Initialize();
}

// 프레임워크로부터 들어오는 모든 이벤트의 진입점 (Component의 순수가상함수 구현).
// 입력: event - 프레임워크가 보낸 이벤트 (HTTP 요청, MetadataManager 응답 등)
// 출력: bool - 이 이벤트를 처리했음을 나타내는 값 (항상 true)
// 동작: 이벤트 타입에 따라 HandleHttpRequest / ProcessMetadata로 분기하고,
//       모르는 타입은 Component::ProcessAEvent(기본 처리)로 넘김
bool SampleComponent::ProcessAEvent(Event* event) {
  switch (event->GetType()) {
    case (int32_t)IAppDispatcher::EEventType::eHttpRequest: {
      HandleHttpRequest(event);
      break;
    }
    case static_cast<int32_t>(IPMetadataManager::EEventType::eMetadataRequest):
      ProcessMetadata(event);
      break;
    default:
      Component::ProcessAEvent(event);
      break;
  }
  return true;
}

// AppDispatcher에게 "/testsend" 경로(POST 메서드)를 이 컴포넌트가 처리하겠다고 등록한다.
// 입력: 없음 / 출력: 없음(void)
// 동작: 등록 요청 이벤트를 "AppDispatcher"로 보냄 (응답을 기다리지 않는 fire-and-forget)
void SampleComponent::RegisterURI() {
  printf("[SampleComponent] Register URI\n");

  Vector<String> methods;
  methods.push_back("POST");

  auto* testsend_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/testsend"), GetInstanceName(), methods);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, testsend_uri);
}

// "/testsend" HTTP 요청을 실제로 처리하는 핸들러.
// 입력: event - HTTP 요청이 담긴 이벤트. 요청 body(JSON)에 "marker_count"(정수, 선택, 없으면 기본값 3) 포함 가능
// 출력: bool - 항상 true(이벤트 처리 완료). 실제 HTTP 응답은 oas->SetResponseBody로 내려보냄
//       응답 JSON 형태: { ok, channel, marker_ids: [...],
//                         markers: [{ id, corners: [x0,y0,x1,y1,x2,y2,x3,y3] }, ...] }
// 동작: 1) marker_count만큼 GenerateRandomMarkers로 가짜 검출 결과 생성
//       2) SendMetadata로 실제 전송 경로(MetadataManager)를 그대로 태움
//       3) 방금 보낸 내용을 JSON으로 그대로 돌려줘서 웹페이지가 로그에 찍을 수 있게 함
bool SampleComponent::HandleHttpRequest(Event* event) {
  if (event->IsReply()) {
    return true;
  }

  auto* oas = reinterpret_cast<OpenAppSerializable*>(event->GetBaseObjectArgument());
  auto path_info = oas->GetFCGXParam("PATH_INFO");

  if (path_info != "/testsend") {
    oas->SetStatusCode(404);
    oas->SetResponseBody("unsupported path");
    return true;
  }

  auto body = oas->GetRequestBody();
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  doc.Parse(body);

  int marker_count = 3;
  if (!doc.HasParseError() && doc.HasMember("marker_count")) {
    marker_count = doc["marker_count"].GetInt();
  }

  std::vector<int> ids;
  std::vector<std::vector<MarkerMetadataFormat::Point2f>> corners;
  GenerateRandomMarkers(marker_count, ids, corners);

  // 실제 운영 경로(SendMetadata -> MetadataManager)를 그대로 태워서 보냄 —
  // 수신측이 진짜로 이 형식의 메타데이터를 받을 수 있는지까지 검증하기 위함.
  SendMetadata(ids, corners);

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("channel", GetChannel(), alloc);

  auto ids_arr = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
  for (int id : ids) ids_arr.PushBack(id, alloc);
  res.AddMember("marker_ids", ids_arr, alloc);

  auto markers_arr = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
  for (size_t i = 0; i < ids.size(); ++i) {
    auto marker = JsonUtility::ValueType(JsonUtility::Type::kObjectType);
    marker.AddMember("id", ids[i], alloc);
    auto corner_arr = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
    for (const auto& pt : corners[i]) {
      corner_arr.PushBack(pt.x, alloc);
      corner_arr.PushBack(pt.y, alloc);
    }
    marker.AddMember("corners", corner_arr, alloc);
    markers_arr.PushBack(marker, alloc);
  }
  res.AddMember("markers", markers_arr, alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  res.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
  return true;
}

// MetadataManager가 우리가 보낸 메타데이터를 처리한 뒤 돌려주는 echo(확인) 이벤트를 받는 핸들러.
// 입력: event - MetadataManager로부터 온 응답 이벤트 (nullptr이거나 reply가 아니면 그냥 무시)
// 출력: 없음(void). 콘솔 로그에 채널 번호/처리 결과 텍스트만 찍음 (앱 동작에 영향 없는
//       순수 디버깅용 — 우리가 보낸 게 실제로 전달됐는지 눈으로 확인하기 위함)
void SampleComponent::ProcessMetadata(Event* event) {
  if (event == nullptr || event->IsReply()) {
    return;
  }

  auto attachment = event->GetAttachment<IPMetadataManager::MetadataOutput>();
  if (attachment) {
    std::cout << "[DummyArUco][MetadataEcho] channel=" << attachment->channel() << " output=" << attachment->output()
               << std::endl;
  }
}

// 마커 id/코너를 ONVIF 메타데이터 XML로 만들어 MetadataManager에게 실제로 전송한다.
// 입력: ids     - 마커 id 목록
//       corners - 마커별 코너 4점 목록 (ids와 같은 인덱스로 1:1 대응되어야 함)
// 출력: 없음(void). SendNoReplyEvent로 보내고 끝남 (응답을 기다리지 않음 — 전송 성공/실패
//       여부는 위 ProcessMetadata의 echo 로그로만 간접 확인 가능)
void SampleComponent::SendMetadata(const std::vector<int>& ids, const std::vector<std::vector<MarkerMetadataFormat::Point2f>>& corners) {
  auto now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

  std::string xml = MarkerMetadataFormat::BuildMarkerMetadataXml(GetChannel(), ids, corners, now_ms);

  auto metadata = StringMetadata(GetChannel(), now_ms);
  metadata.Set(xml);

  auto* req = new ("MetadataRequest") IPMetadataManager::StringMetadataRequest();
  req->SetStringMetadata(std::move(metadata));

  SendNoReplyEvent("MetadataManager", static_cast<int32_t>(IMetadataManager::EEventType::eRequestRawMetadata), 0, req);
}

extern "C" {
// SDK가 이 컴포넌트의 .so 파일을 로드할 때 호출하는 생성 팩토리 함수.
// 입력: mem_manager - 프레임워크가 주는 메모리 관리자 (Component/Event의 커스텀 allocator 초기화용)
// 출력: 새로 생성된 SampleComponent 인스턴스의 포인터
SampleComponent* create_component(void* mem_manager) {
  Component::allocator = decltype(Component::allocator)(mem_manager);
  Event::allocator = decltype(Event::allocator)(mem_manager);
  return new ("SampleComponent") SampleComponent();
}

// 컴포넌트 종료 시 SDK가 호출하는 소멸 팩토리 함수.
// 입력: ptr - 삭제할 SampleComponent 인스턴스 포인터
// 출력: 없음(void)
void destroy_component(SampleComponent* ptr) { delete ptr; }
}
