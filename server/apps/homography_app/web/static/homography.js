'use strict';

// 이 화면은 한 가지 흐름만 사용한다.
// 1) CCTV·채널을 선택하고 캡처한다.
// 2) 검출된 ArUco 꼭짓점을 필요할 때만 드래그해 보정한다.
// 3) 마커 크기(mm)로 해당 스트림의 카메라 화면 펴기 H를 산출한다.
// 4) 준비된 모든 스트림을 공통 마커로 한 번에 전체 맵에 정합한다.
// X/Y 기준선, 자로 잰 거리, 별도 프리뷰 단계는 사용하지 않는다.

const $ = (selector) => document.querySelector(selector);
const canvas = $('#work-canvas');
const context = canvas.getContext('2d');
const viewport = $('#canvas-viewport');

const state = {
  status: null,
  streams: [],
  captures: new Map(),
  selectedCameraId: null,
  selectedChannel: 1,
  currentStreamId: null,
  currentCapture: null,
  view: {scale: 1, x: 0, y: 0},
  pointer: null,
  verification: null,
  verificationGl: null,
  verificationView: {scale: 1, x: 0, y: 0},
  verificationPointer: null,
  blinkTimer: null,
  blinkPhase: 0,
  blinkEnabled: false,
};

const MIN_LOCAL_MARKERS = 3;
const VERIFICATION_COLORS = ['#3e9bff', '#ff9d3e', '#51d88b', '#d36cff', '#f15b8a', '#b9a44c'];

function verificationColor(index) {
  return VERIFICATION_COLORS[index % VERIFICATION_COLORS.length];
}

function applyVerificationView() {
  const transform = `translate3d(${state.verificationView.x}px, ${state.verificationView.y}px, 0) scale(${state.verificationView.scale})`;
  $('#verification-canvas').style.transform = transform;
  $('#verification-overlay').style.transform = transform;
}

function resetVerificationView() {
  state.verificationView = {scale: 1, x: 0, y: 0};
  applyVerificationView();
}

function log(value) {
  $('#result-log').textContent = typeof value === 'string'
    ? value : JSON.stringify(value, null, 2);
}

async function getJson(path) {
  const response = await fetch(path, {cache: 'no-store'});
  const value = await response.json();
  if (!response.ok || value.ok === false)
    throw new Error(value.error || `HTTP ${response.status}`);
  return value;
}

async function post(path, body) {
  const response = await fetch(path, {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(body),
  });
  const value = await response.json();
  if (!response.ok || value.ok === false)
    throw new Error(value.error || value.stderr || `HTTP ${response.status}`);
  return value;
}

function streamFor(cameraId, channel) {
  return state.streams.find((stream) =>
    stream.camera_id === cameraId && Number(stream.channel) === Number(channel));
}

function cameraFor(cameraId) {
  return (state.status?.camera_list || []).find((camera) =>
    camera.camera_id === cameraId);
}

function selectedStream() {
  return streamFor(state.selectedCameraId, state.selectedChannel);
}

function point(value) {
  if (Array.isArray(value)) return {x: Number(value[0]), y: Number(value[1])};
  return {x: Number(value.x), y: Number(value.y)};
}

function validPoint(value) {
  const result = point(value);
  return Number.isFinite(result.x) && Number.isFinite(result.y);
}

function rawCorners(capture, index) {
  const values = capture.capture.corners?.[index] || [];
  return values.map(point).filter(validPoint);
}

function markerCorners(capture, index) {
  const id = Number(capture.capture.ids[index]);
  const override = capture.overrides[String(id)];
  return override ? override.map(point) : rawCorners(capture, index);
}

function usableMarkerIds(capture) {
  return capture.capture.ids
    .map(Number)
    .filter((id) => !capture.excluded.has(id));
}

function ensureReferenceMarker(capture) {
  const ids = usableMarkerIds(capture);
  if (!ids.includes(Number(capture.referenceMarkerId)))
    capture.referenceMarkerId = ids[0] ?? null;
}

function populateCameraSelectors() {
  const cameras = state.status?.camera_list || [];
  const cameraSelect = $('#capture-camera-select');
  const settingsSelect = $('#camera-list');
  const options = cameras.map((camera) =>
    `<option value="${camera.camera_id}">${camera.camera_id} · ${camera.camera_model}</option>`).join('');
  cameraSelect.innerHTML = options;
  settingsSelect.innerHTML = options;

  if (!state.selectedCameraId || !cameras.some((camera) =>
      camera.camera_id === state.selectedCameraId))
    state.selectedCameraId = cameras[0]?.camera_id || null;
  cameraSelect.value = state.selectedCameraId || '';
  settingsSelect.value = state.selectedCameraId || '';
  populateChannelSelector();
  populateModelSelector();
  updateCameraSettingsFields();
}

function populateChannelSelector() {
  const select = $('#capture-channel-select');
  const channels = state.streams
    .filter((stream) => stream.camera_id === state.selectedCameraId)
    .sort((left, right) => left.channel - right.channel);
  select.innerHTML = channels.map((stream) =>
    `<option value="${stream.channel}">채널 ${stream.channel} · ${stream.stream_id}</option>`).join('');
  if (!channels.some((stream) => Number(stream.channel) === Number(state.selectedChannel)))
    state.selectedChannel = channels[0]?.channel || 1;
  select.value = String(state.selectedChannel);
  showSelectedStream();
}

function populateModelSelector() {
  const select = $('#camera-model');
  select.innerHTML = (state.status?.camera_models || []).map((model) =>
    `<option value="${model.model}">${model.model} · ${model.channel_count}채널</option>`).join('');
}

function updateCameraSettingsFields() {
  const camera = cameraFor(state.selectedCameraId);
  $('#camera-id').value = camera?.camera_id || '';
  $('#camera-model').value = camera?.camera_model || '';
}

function updateHeader() {
  const model = cameraFor(state.selectedCameraId);
  $('#camera-identity').textContent = model
    ? `${model.camera_id} · ${model.camera_model} · ${model.channel_count}채널`
    : '등록된 카메라가 없습니다.';
  $('#support-badge').textContent = `${state.streams.length}개 스트림 · 최대 ${state.status?.max_verification_streams || 0}개 검증`;
}

function resizeCanvas() {
  const rect = viewport.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.round(rect.width * ratio));
  canvas.height = Math.max(1, Math.round(rect.height * ratio));
  canvas.style.width = `${rect.width}px`;
  canvas.style.height = `${rect.height}px`;
  drawCapture();
}

function imageSize(capture) {
  if (!capture?.image) return null;
  return {width: capture.image.naturalWidth, height: capture.image.naturalHeight};
}

function fitCapture() {
  const size = imageSize(state.currentCapture);
  if (!size) return;
  const rect = viewport.getBoundingClientRect();
  state.view.scale = Math.min(rect.width / size.width, rect.height / size.height);
  state.view.x = (rect.width - size.width * state.view.scale) / 2;
  state.view.y = (rect.height - size.height * state.view.scale) / 2;
  drawCapture();
}

function screenPoint(event) {
  const rect = canvas.getBoundingClientRect();
  return {x: event.clientX - rect.left, y: event.clientY - rect.top};
}

function imagePoint(screen) {
  return {
    x: (screen.x - state.view.x) / state.view.scale,
    y: (screen.y - state.view.y) / state.view.scale,
  };
}

function screenFromImage(image) {
  return {
    x: state.view.x + image.x * state.view.scale,
    y: state.view.y + image.y * state.view.scale,
  };
}

function nearestCorner(screen) {
  const capture = state.currentCapture;
  if (!capture) return null;
  let nearest = null;
  let distance = 13;
  capture.capture.ids.forEach((id, markerIndex) => {
    markerCorners(capture, markerIndex).forEach((corner, cornerIndex) => {
      const current = screenFromImage(corner);
      const measured = Math.hypot(current.x - screen.x, current.y - screen.y);
      if (measured < distance)
        nearest = {id: Number(id), markerIndex, cornerIndex, distance: measured};
    });
  });
  return nearest;
}

function drawCapture() {
  const ratio = window.devicePixelRatio || 1;
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  const rect = viewport.getBoundingClientRect();
  context.clearRect(0, 0, rect.width, rect.height);
  const capture = state.currentCapture;
  $('#empty-state').hidden = Boolean(capture?.image);
  if (!capture?.image) return;

  context.drawImage(capture.image, state.view.x, state.view.y,
                   capture.image.naturalWidth * state.view.scale,
                   capture.image.naturalHeight * state.view.scale);

  capture.capture.ids.forEach((id, markerIndex) => {
    const corners = markerCorners(capture, markerIndex);
    if (corners.length !== 4) return;
    const selected = Number(id) === Number(capture.referenceMarkerId);
    const excluded = capture.excluded.has(Number(id));
    const color = excluded ? 'rgba(145,155,165,.45)' :
      selected ? 'rgba(255,174,70,.92)' : 'rgba(57,224,157,.72)';
    const points = corners.map(screenFromImage);
    context.save();
    context.strokeStyle = color;
    context.lineWidth = selected ? 2 : 1.2;
    context.beginPath();
    context.moveTo(points[0].x, points[0].y);
    points.slice(1).forEach((value) => context.lineTo(value.x, value.y));
    context.closePath();
    context.stroke();
    points.forEach((value) => {
      context.fillStyle = color;
      context.beginPath();
      context.arc(value.x, value.y, selected ? 3.8 : 3, 0, Math.PI * 2);
      context.fill();
    });
    context.fillStyle = color;
    context.font = '700 11px system-ui';
    context.fillText(`ID ${id}`, points[0].x + 6, points[0].y - 6);
    context.restore();
  });
}

function showSelectedStream() {
  const stream = selectedStream();
  state.currentStreamId = stream?.stream_id || null;
  state.currentCapture = state.currentStreamId
    ? state.captures.get(state.currentStreamId) || null : null;
  if (state.currentCapture) {
    fitCapture();
    renderMarkerList();
    updateLocalResult();
  } else {
    drawCapture();
    renderMarkerList();
    updateLocalResult();
  }
  updateGlobalState();
}

function renderMarkerList() {
  const container = $('#marker-list');
  const capture = state.currentCapture;
  if (!capture) {
    container.innerHTML = '';
    $('#marker-count').textContent = '0';
    $('#marker-help').textContent = '먼저 캡처할 스트림을 선택하세요.';
    return;
  }
  ensureReferenceMarker(capture);
  const ids = capture.capture.ids.map(Number);
  $('#marker-count').textContent = String(ids.length);
  $('#marker-help').textContent = `기준 마커 ${capture.referenceMarkerId ?? '—'} · 꼭짓점은 캔버스에서 드래그할 수 있습니다.`;
  container.innerHTML = ids.map((id) => {
    const excluded = capture.excluded.has(id);
    const reference = Number(capture.referenceMarkerId) === id;
    return `<div class="marker-row ${reference ? 'reference' : ''}">
      <input type="checkbox" data-use-marker="${id}" ${excluded ? '' : 'checked'} aria-label="ID ${id} 사용">
      <strong>ID ${id}</strong>
      <button type="button" data-reference-marker="${id}">${reference ? '기준 마커' : '기준으로 선택'}</button>
    </div>`;
  }).join('');
}

function updateLocalResult() {
  const capture = state.currentCapture;
  const result = $('#local-result');
  const button = $('#solve-homography');
  if (!capture) {
    button.disabled = true;
    result.textContent = '아직 산출하지 않았습니다.';
    return;
  }
  const usable = usableMarkerIds(capture).length;
  const markerSize = Number($('#marker-size-mm').value);
  button.disabled = usable < MIN_LOCAL_MARKERS || !Number.isFinite(markerSize) || markerSize <= 0;
  if (capture.localResult) {
    result.innerHTML = `<strong>카메라 화면 펴기 완료</strong><br>사용 마커 ${capture.localResult.used_marker_count ?? usable}개 · 마커 모양 오차 ${Number(capture.localResult.marker_shape_rmse_mm ?? 0).toFixed(2)} mm<br><small>겹침 구간 연결 전까지 임시 결과입니다.</small>`;
  } else {
    result.textContent = `${usable}개 사용 가능 · 최소 ${MIN_LOCAL_MARKERS}개가 필요합니다.`;
  }
}

async function captureSelected() {
  const stream = selectedStream();
  if (!stream) return;
  const button = $('#capture-camera');
  button.disabled = true;
  $('#camera-status').textContent = `${stream.stream_id} 캡처 중…`;
  try {
    const payload = await post('/api/camera/detect', {stream_id: stream.stream_id});
    const detected = payload.result;
    const image = await loadImage(`${detected.image_url}?t=${Date.now()}`);
    const capture = {
      stream,
      capture: detected,
      image,
      overrides: {},
      excluded: new Set(),
      referenceMarkerId: Number(detected.ids?.[0]),
      localResult: null,
    };
    state.captures.set(stream.stream_id, capture);
    state.currentStreamId = stream.stream_id;
    state.currentCapture = capture;
    fitCapture();
    renderMarkerList();
    updateLocalResult();
    updateGlobalState();
    $('#camera-status').textContent = `${stream.stream_id} 캡처 완료 · 검출 ID ${detected.ids.join(', ') || '없음'}`;
    log({stream_id: stream.stream_id, capture_id: detected.capture_id,
      detected_ids: detected.ids, image_size: detected.image_size,
      rtsp_alignment: detected.rtsp_alignment || detected.rtsp_alignment_error});
  } catch (error) {
    $('#camera-status').textContent = `캡처 실패: ${error.message}`;
    log(error.message);
  } finally {
    button.disabled = false;
  }
}

async function solveLocal() {
  const capture = state.currentCapture;
  if (!capture) return;
  ensureReferenceMarker(capture);
  const markerSize = Number($('#marker-size-mm').value);
  if (usableMarkerIds(capture).length < MIN_LOCAL_MARKERS || !capture.referenceMarkerId) return;
  const button = $('#solve-homography');
  button.disabled = true;
  $('#camera-status').textContent = `${capture.stream.stream_id} 카메라 화면 펴기 중…`;
  try {
    const payload = await post('/api/homography/solve', {
      capture_id: capture.capture.capture_id,
      marker_size_mm: markerSize,
      reference_marker_id: Number(capture.referenceMarkerId),
      excluded_ids: [...capture.excluded],
      corner_overrides: capture.overrides,
    });
    capture.localResult = payload.result;
    updateLocalResult();
    updateGlobalState();
    $('#camera-status').textContent = `${capture.stream.stream_id} 카메라 화면 펴기 완료 · 마커 모양 오차 ${Number(payload.result.marker_shape_rmse_mm ?? 0).toFixed(2)} mm`;
    log({stream_id: capture.stream.stream_id, camera_pixels_to_channel_map: payload.result,
      note: '겹침 구간 연결을 완료해야 운영 H에 저장됩니다.'});
  } catch (error) {
    $('#camera-status').textContent = `카메라 화면 펴기 실패: ${error.message}`;
    log(error.message);
    updateLocalResult();
  }
}

function updateGlobalState() {
  const ready = state.streams.filter((stream) => state.captures.get(stream.stream_id)?.localResult);
  const missing = state.streams.length - ready.length;
  $('#common-marker-summary').textContent = ready.length < 2
    ? `카메라 화면 펴기 준비 ${ready.length}개 · 최소 2개 스트림이 필요합니다.`
    : `카메라 화면 펴기 준비 ${ready.length}/${state.streams.length}개 · 준비된 스트림의 겹침 구간을 연결합니다.${missing ? ` 미준비 ${missing}개는 제외됩니다.` : ''}`;
  $('#global-align-channels').disabled = ready.length < 2;
  const anchor = $('#global-anchor-stream');
  const previous = anchor.value;
  anchor.innerHTML = ready.map((stream) =>
    `<option value="${stream.stream_id}">${stream.stream_id}</option>`).join('');
  anchor.value = ready.some((stream) => stream.stream_id === previous)
    ? previous : (ready[0]?.stream_id || '');
}

async function alignAllStreams() {
  const ready = state.streams.filter((stream) => state.captures.get(stream.stream_id)?.localResult);
  if (ready.length < 2) return;
  const anchor = $('#global-anchor-stream').value || ready[0].stream_id;
  const captureIds = {};
  ready.forEach((stream) => { captureIds[stream.stream_id] = state.captures.get(stream.stream_id).capture.capture_id; });
  const button = $('#global-align-channels');
  button.disabled = true;
  $('#stitch-result').textContent = '겹침 구간 연결 중…';
  try {
    const payload = await post('/api/homography/global-align', {
      stream_ids: ready.map((stream) => stream.stream_id),
      anchor_stream_id: anchor,
      capture_ids: captureIds,
    });
    $('#stitch-result').innerHTML = `<strong>겹침 구간 연결 완료</strong><br>기준 ${payload.anchor_stream_id} · 겹침 맞춤 오차 ${Number(payload.shared_map_overlap_rmse_mm ?? 0).toFixed(2)} mm<br>저장 스트림 ${payload.stream_ids.length}개`;
    log(payload);
    await prepareVerification(payload);
  } catch (error) {
    $('#stitch-result').textContent = `겹침 구간 연결 실패: ${error.message}`;
    $('#verification-status').textContent = `마커 포개기 준비 실패: ${error.message}`;
    log(error.message);
  } finally {
    updateGlobalState();
  }
}

function loadImage(url) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error(`이미지를 불러오지 못했습니다: ${url}`));
    image.src = url;
  });
}

async function loadCapturedMarkers(stream) {
  if (stream.markers || !stream.capture_id) return stream;
  const detected = await getJson(`/artifacts/${stream.capture_id}/markers.json`);
  let layout = {};
  try {
    layout = await getJson(`/artifacts/${stream.capture_id}/layout.json`);
  } catch (_) {
    // 예전 캡처에는 layout.json이 없을 수 있다. 자동 검출 코너를 그대로 사용한다.
  }
  const overrides = layout.corner_overrides || {};
  const excluded = new Set((layout.excluded_ids || []).map(Number));
  const markers = {};
  (detected.ids || []).forEach((id, index) => {
    const numericId = Number(id);
    if (excluded.has(numericId)) return;
    markers[String(numericId)] = overrides[String(numericId)] || detected.corners[index];
  });
  return {...stream, markers};
}

function compileShader(gl, type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const error = gl.getShaderInfoLog(shader);
    gl.deleteShader(shader);
    throw new Error(error || '검증 화면 셰이더 생성 실패');
  }
  return shader;
}

function createVerificationGl() {
  if (state.verificationGl) return state.verificationGl;
  const target = $('#verification-canvas');
  const gl = target.getContext('webgl', {alpha: true, premultipliedAlpha: false});
  if (!gl) return null;
  const vertex = compileShader(gl, gl.VERTEX_SHADER, `
    attribute vec3 a_world;
    attribute vec2 a_texcoord;
    uniform vec4 u_bounds;
    varying vec2 v_texcoord;
    void main() {
      float x = 2.0 * (a_world.x - u_bounds.x * a_world.z) / u_bounds.z - a_world.z;
      float y = a_world.z - 2.0 * (a_world.y - u_bounds.y * a_world.z) / u_bounds.w;
      gl_Position = vec4(x, y, 0.0, a_world.z);
      v_texcoord = a_texcoord;
    }
  `);
  const fragment = compileShader(gl, gl.FRAGMENT_SHADER, `
    precision mediump float;
    uniform sampler2D u_image;
    uniform float u_opacity;
    uniform float u_feather;
    varying vec2 v_texcoord;
    void main() {
      vec4 color = texture2D(u_image, v_texcoord);
      float edge = min(min(v_texcoord.x, 1.0 - v_texcoord.x),
                       min(v_texcoord.y, 1.0 - v_texcoord.y));
      float feather = u_feather > 0.5 ? smoothstep(0.0, 0.14, edge) : 1.0;
      gl_FragColor = vec4(color.rgb, color.a * u_opacity * feather);
    }
  `);
  const program = gl.createProgram();
  gl.attachShader(program, vertex);
  gl.attachShader(program, fragment);
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS))
    throw new Error(gl.getProgramInfoLog(program) || '검증 화면 프로그램 연결 실패');
  state.verificationGl = {
    gl, program,
    world: gl.getAttribLocation(program, 'a_world'),
    texcoord: gl.getAttribLocation(program, 'a_texcoord'),
    bounds: gl.getUniformLocation(program, 'u_bounds'),
    opacity: gl.getUniformLocation(program, 'u_opacity'),
    feather: gl.getUniformLocation(program, 'u_feather'),
  };
  return state.verificationGl;
}

function homographyWorldPoint(matrix, x, y) {
  const w = matrix[2][0] * x + matrix[2][1] * y + matrix[2][2];
  return {
    x: matrix[0][0] * x + matrix[0][1] * y + matrix[0][2],
    y: matrix[1][0] * x + matrix[1][1] * y + matrix[1][2],
    w,
  };
}

function projectedStreamCorners(stream) {
  return [[0, 0], [stream.image_size.width, 0],
    [stream.image_size.width, stream.image_size.height], [0, stream.image_size.height]]
    .map(([x, y]) => homographyWorldPoint(stream.H_camera_pixels_to_shared_map, x, y))
    .map((value) => ({x: value.x / value.w, y: value.y / value.w}))
    .filter((value) => Number.isFinite(value.x) && Number.isFinite(value.y));
}

function worldToVerificationScreen(x, y, bounds, rect) {
  const width = bounds.max_x - bounds.min_x || 1;
  const height = bounds.max_y - bounds.min_y || 1;
  return {
    x: (x - bounds.min_x) * rect.width / width,
    y: (y - bounds.min_y) * rect.height / height,
  };
}

function niceGridStep(span) {
  const rough = Math.max(Math.abs(span) / 8, 1);
  const power = 10 ** Math.floor(Math.log10(rough));
  const normalized = rough / power;
  return (normalized < 2 ? 1 : normalized < 5 ? 2 : 5) * power;
}

/* 영상 위에 맵의 기준과 마커를 그려, 합쳐진 결과를 사람이 읽을 수 있게 한다. */
function drawVerificationGuides() {
  if (!state.verification) return;
  const overlay = $('#verification-overlay');
  const rect = $('#verification-viewport').getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  overlay.width = Math.max(1, Math.round(rect.width * ratio));
  overlay.height = Math.max(1, Math.round(rect.height * ratio));
  overlay.style.width = `${rect.width}px`;
  overlay.style.height = `${rect.height}px`;
  const drawing = overlay.getContext('2d');
  const bounds = state.verification.bounds;
  const mapWidth = bounds.max_x - bounds.min_x || 1;
  const mapHeight = bounds.max_y - bounds.min_y || 1;
  drawing.setTransform(ratio, 0, 0, ratio, 0, 0);
  drawing.clearRect(0, 0, rect.width, rect.height);

  // 전체 맵의 mm 격자: 사진 위에서 실제 좌표 방향과 크기를 가늠할 수 있다.
  drawing.save();
  drawing.strokeStyle = 'rgba(190, 220, 248, 0.18)';
  drawing.fillStyle = 'rgba(222, 237, 251, 0.72)';
  drawing.lineWidth = 1;
  drawing.font = '11px system-ui, sans-serif';
  drawing.textBaseline = 'top';
  const xStep = niceGridStep(mapWidth);
  const yStep = niceGridStep(mapHeight);
  const firstX = Math.ceil(bounds.min_x / xStep) * xStep;
  const firstY = Math.ceil(bounds.min_y / yStep) * yStep;
  for (let value = firstX; value <= bounds.max_x; value += xStep) {
    const screen = worldToVerificationScreen(value, bounds.min_y, bounds, rect);
    drawing.beginPath();
    drawing.moveTo(screen.x, 0);
    drawing.lineTo(screen.x, rect.height);
    drawing.stroke();
    drawing.fillText(`${Math.round(value)} mm`, screen.x + 4, 4);
  }
  for (let value = firstY; value <= bounds.max_y; value += yStep) {
    const screen = worldToVerificationScreen(bounds.min_x, value, bounds, rect);
    drawing.beginPath();
    drawing.moveTo(0, screen.y);
    drawing.lineTo(rect.width, screen.y);
    drawing.stroke();
    drawing.fillText(`${Math.round(value)} mm`, 4, screen.y + 4);
  }
  drawing.restore();

  const visibleIndex = state.blinkPhase % Math.max(1, state.verification.streams.length);
  state.verification.streams.forEach((stream, index) => {
    if (state.blinkEnabled && index !== visibleIndex) return;
    const corners = projectedStreamCorners(stream).map((value) =>
      worldToVerificationScreen(value.x, value.y, bounds, rect));
    if (corners.length !== 4) return;
    const color = verificationColor(index);
    drawing.save();
    drawing.beginPath();
    drawing.moveTo(corners[0].x, corners[0].y);
    corners.slice(1).forEach((value) => drawing.lineTo(value.x, value.y));
    drawing.closePath();
    // 어두운 외곽선을 먼저 그려 어떤 영상 위에서도 경계가 보이게 한다.
    drawing.strokeStyle = 'rgba(0, 0, 0, 0.85)';
    drawing.lineWidth = 6;
    drawing.stroke();
    drawing.strokeStyle = color;
    drawing.lineWidth = 2.5;
    drawing.stroke();
    corners.forEach((value) => {
      drawing.fillStyle = color;
      drawing.beginPath();
      drawing.arc(value.x, value.y, 4, 0, Math.PI * 2);
      drawing.fill();
    });
    const label = stream.stream_id;
    const labelPoint = corners[0];
    drawing.font = '700 12px system-ui, sans-serif';
    const labelWidth = drawing.measureText(label).width + 12;
    const labelX = Math.max(3, Math.min(rect.width - labelWidth - 3, labelPoint.x + 6));
    const labelY = Math.max(3, Math.min(rect.height - 22, labelPoint.y + 6));
    drawing.fillStyle = 'rgba(3, 9, 15, 0.88)';
    drawing.fillRect(labelX, labelY, labelWidth, 20);
    drawing.fillStyle = color;
    drawing.textBaseline = 'middle';
    drawing.fillText(label, labelX + 6, labelY + 10);
    drawing.restore();
  });

  // 여러 채널에서 공통으로 보인 마커는 전체 맵 기준 위치에 크게 표시한다.
  drawing.save();
  (state.verification.common_markers || []).forEach((marker) => {
    const screen = worldToVerificationScreen(marker.x, marker.y, bounds, rect);
    drawing.strokeStyle = '#fff';
    drawing.lineWidth = 2;
    drawing.beginPath();
    drawing.arc(screen.x, screen.y, 9, 0, Math.PI * 2);
    drawing.stroke();
    drawing.beginPath();
    drawing.moveTo(screen.x - 13, screen.y);
    drawing.lineTo(screen.x + 13, screen.y);
    drawing.moveTo(screen.x, screen.y - 13);
    drawing.lineTo(screen.x, screen.y + 13);
    drawing.stroke();
    const label = `ID ${marker.id}`;
    drawing.font = '700 12px system-ui, sans-serif';
    const labelWidth = drawing.measureText(label).width + 10;
    const labelX = Math.max(3, Math.min(rect.width - labelWidth - 3, screen.x + 12));
    const labelY = Math.max(3, Math.min(rect.height - 20, screen.y - 10));
    drawing.fillStyle = 'rgba(255, 255, 255, 0.92)';
    drawing.fillRect(labelX, labelY, labelWidth, 18);
    drawing.fillStyle = '#0b1724';
    drawing.textBaseline = 'middle';
    drawing.fillText(label, labelX + 5, labelY + 9);
  });
  drawing.restore();
}

function drawVerificationFallback() {
  const target = $('#verification-canvas');
  const rect = $('#verification-viewport').getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  target.width = Math.max(1, Math.round(rect.width * ratio));
  target.height = Math.max(1, Math.round(rect.height * ratio));
  target.style.width = `${rect.width}px`;
  target.style.height = `${rect.height}px`;
  const drawing = target.getContext('2d');
  drawing.setTransform(ratio, 0, 0, ratio, 0, 0);
  drawing.fillStyle = '#05080b';
  drawing.fillRect(0, 0, rect.width, rect.height);
  drawVerificationGuides();
  $('#verification-status').textContent = 'WebGL을 사용할 수 없어 정합 가이드만 표시합니다.';
}

function drawMarkerVerificationGuides(markerId, streams) {
  const overlay = $('#verification-overlay');
  const rect = $('#verification-viewport').getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  overlay.width = Math.max(1, Math.round(rect.width * ratio));
  overlay.height = Math.max(1, Math.round(rect.height * ratio));
  overlay.style.width = `${rect.width}px`;
  overlay.style.height = `${rect.height}px`;
  const drawing = overlay.getContext('2d');
  drawing.setTransform(ratio, 0, 0, ratio, 0, 0);
  drawing.clearRect(0, 0, rect.width, rect.height);
  const left = rect.width * 0.16;
  const top = rect.height * 0.08;
  const size = Math.min(rect.width * 0.68, rect.height * 0.78);

  drawing.fillStyle = 'rgba(0, 0, 0, 0.78)';
  drawing.fillRect(left - 5, top - 5, size + 10, size + 10);
  drawing.strokeStyle = '#fff';
  drawing.lineWidth = 2;
  drawing.strokeRect(left, top, size, size);
  drawing.fillStyle = '#fff';
  drawing.font = '700 14px system-ui, sans-serif';
  drawing.fillText(`공통 마커 ID ${markerId}`, left, Math.max(20, top - 12));

  streams.forEach((stream, index) => {
    const color = verificationColor(index);
    const label = stream.stream_id;
    const y = Math.min(rect.height - 18, top + size + 18 + index * 20);
    drawing.fillStyle = color;
    drawing.beginPath();
    drawing.arc(left + 5, y - 4, 4, 0, Math.PI * 2);
    drawing.fill();
    drawing.fillStyle = '#e7eff8';
    drawing.font = '12px system-ui, sans-serif';
    drawing.fillText(label, left + 15, y);
  });
}

/* 선택한 같은 ID의 마커 사각형만 동일한 화면 사각형에 겹쳐 그린다. */
function drawMarkerVerification() {
  const markerId = Number(state.verification.marker_id);
  const streams = state.verification.streams.filter((stream) =>
    stream.markers?.[String(markerId)] && stream.image);
  if (!streams.length) return;

  let renderer;
  try { renderer = createVerificationGl(); } catch (error) {
    $('#verification-status').textContent = error.message;
    drawMarkerVerificationGuides(markerId, streams);
    return;
  }
  if (!renderer) {
    drawMarkerVerificationGuides(markerId, streams);
    $('#verification-status').textContent = 'WebGL을 사용할 수 없어 마커 가이드만 표시합니다.';
    return;
  }

  const {gl, program} = renderer;
  const target = $('#verification-canvas');
  const rect = $('#verification-viewport').getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  target.width = Math.max(1, Math.round(rect.width * ratio));
  target.height = Math.max(1, Math.round(rect.height * ratio));
  target.style.width = `${rect.width}px`;
  target.style.height = `${rect.height}px`;
  gl.viewport(0, 0, target.width, target.height);
  gl.clearColor(0.02, 0.04, 0.07, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.enable(gl.BLEND);
  gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  gl.useProgram(program);

  // 영상의 H나 전체 맵 크기는 사용하지 않는다.
  // 모든 입력 마커를 이 하나의 기준 정사각형에 직접 워핑한다.
  const bounds = {min_x: -1.1, min_y: -1.1, max_x: 1.1, max_y: 1.1};
  const markerSquare = [[-0.86, -0.86, 1], [0.86, -0.86, 1],
    [0.86, 0.86, 1], [-0.86, 0.86, 1]];
  streams.forEach((stream) => {
    const corners = stream.markers[String(markerId)];
    const width = stream.image_size.width;
    const height = stream.image_size.height;
    const worldBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, worldBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(markerSquare.flat()), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(renderer.world);
    gl.vertexAttribPointer(renderer.world, 3, gl.FLOAT, false, 0, 0);

    const texcoordBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, texcoordBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(corners.flatMap((corner) =>
      [Number(corner.x) / width, Number(corner.y) / height])), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(renderer.texcoord);
    gl.vertexAttribPointer(renderer.texcoord, 2, gl.FLOAT, false, 0, 0);

    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, stream.image);
    gl.uniform4f(renderer.bounds, bounds.min_x, bounds.min_y,
      bounds.max_x - bounds.min_x, bounds.max_y - bounds.min_y);
    // 조작 가능한 투명도는 없애고, 고정된 반투명으로 모든 채널을 동시에 보인다.
    gl.uniform1f(renderer.opacity, 0.5);
    gl.uniform1f(renderer.feather, 0);
    gl.drawArrays(gl.TRIANGLE_FAN, 0, 4);
    gl.deleteBuffer(worldBuffer);
    gl.deleteBuffer(texcoordBuffer);
    gl.deleteTexture(texture);
  });
  drawMarkerVerificationGuides(markerId, streams);
  $('#verification-status').textContent =
    `ID ${markerId} · ${streams.length}개 채널의 마커 이미지를 같은 정사각형에 포개는 중`;
}

function drawVerification() {
  if (!state.verification) return;
  if (state.verification.mode === 'marker') {
    drawMarkerVerification();
    return;
  }
  let renderer;
  try { renderer = createVerificationGl(); } catch (error) {
    $('#verification-status').textContent = error.message;
    drawVerificationFallback();
    return;
  }
  if (!renderer) { drawVerificationFallback(); return; }
  const {gl, program} = renderer;
  const target = $('#verification-canvas');
  const rect = $('#verification-viewport').getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  target.width = Math.max(1, Math.round(rect.width * ratio));
  target.height = Math.max(1, Math.round(rect.height * ratio));
  target.style.width = `${rect.width}px`;
  target.style.height = `${rect.height}px`;
  gl.viewport(0, 0, target.width, target.height);
  gl.clearColor(0.02, 0.04, 0.07, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.enable(gl.BLEND);
  gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  gl.useProgram(program);
  const bounds = state.verification.bounds;
  const visibleIndex = state.blinkPhase % Math.max(1, state.verification.streams.length);
  state.verification.streams.forEach((stream, index) => {
    if (state.blinkEnabled && index !== visibleIndex) return;
    if (!stream.image) return;
    const width = stream.image_size.width;
    const height = stream.image_size.height;
    const corners = [[0, 0], [width, 0], [width, height], [0, height]]
      .map(([x, y]) => homographyWorldPoint(stream.H_camera_pixels_to_shared_map, x, y));
    const worldBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, worldBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(corners.flatMap((value) =>
      [value.x, value.y, value.w])), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(renderer.world);
    gl.vertexAttribPointer(renderer.world, 3, gl.FLOAT, false, 0, 0);
    const texcoordBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, texcoordBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 1, 1, 0, 1]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(renderer.texcoord);
    gl.vertexAttribPointer(renderer.texcoord, 2, gl.FLOAT, false, 0, 0);
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, stream.image);
    gl.uniform4f(renderer.bounds, bounds.min_x, bounds.min_y,
                 bounds.max_x - bounds.min_x, bounds.max_y - bounds.min_y);
    // 기준 스트림은 선명하게 한 장으로 깔고, 나머지는 가장자리만 부드럽게 섞는다.
    // 모든 영상을 동일한 50%로 겹치던 기존 방식보다 실제 합쳐진 맵이 훨씬 자연스럽다.
    const opacity = state.blinkEnabled || index === 0
      ? 1 : Number($('#verification-opacity').value);
    gl.uniform1f(renderer.opacity, opacity);
    gl.uniform1f(renderer.feather, state.blinkEnabled || index === 0 ? 0 : 1);
    gl.drawArrays(gl.TRIANGLE_FAN, 0, 4);
    gl.deleteBuffer(worldBuffer);
    gl.deleteBuffer(texcoordBuffer);
    gl.deleteTexture(texture);
  });
  drawVerificationGuides();
  $('#verification-status').textContent = state.blinkEnabled
    ? `깜빡임 비교 · ${state.verification.streams[visibleIndex]?.stream_id || ''}`
    : `${state.verification.streams.length}개 스트림을 마커 기준 전체 맵으로 합성 중`;
}

function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>'"]/g, (character) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;',
  }[character]));
}

function formatMm(value) {
  return Number.isFinite(Number(value)) ? `${Number(value).toFixed(2)} mm` : '—';
}

function formatPoint(value) {
  if (!value) return '—';
  return `(${Number(value.x).toFixed(2)}, ${Number(value.y).toFixed(2)})`;
}

function renderVerificationValues(payload) {
  const verification = payload.verification;
  const markers = verification.overlap_marker_consistency || [];
  const markerWorst = markers.reduce((worst, marker) =>
    Number(marker.max_corner_disagreement_mm) > Number(worst?.max_corner_disagreement_mm ?? -1)
      ? marker : worst, null);
  const streamCount = Object.keys(verification.streams || {}).length;
  $('#verification-summary').innerHTML = [
    `<div>기준 스트림<strong>${escapeHtml(payload.anchor_stream_id)}</strong></div>`,
    `<div>검증 스트림<strong>${streamCount}개</strong></div>`,
    `<div>공통 마커<strong>${markers.length}개</strong></div>`,
    `<div>겹침 맞춤 오차<strong>${formatMm(payload.shared_map_overlap_rmse_mm)}</strong></div>`,
    `<div>최대 불일치 마커<strong>${markerWorst ? `ID ${escapeHtml(markerWorst.id)} · ${formatMm(markerWorst.max_corner_disagreement_mm)}` : '—'}</strong></div>`,
  ].join('');

  $('#verification-marker-table').innerHTML = markers.map((marker) => {
    const streams = marker.streams || [];
    const centerMax = streams.reduce((max, stream) =>
      Math.max(max, Number(stream.center_disagreement_mm) || 0), 0);
    const detailRows = streams.map((stream) => `<tr>
      <td>${escapeHtml(stream.stream_id)}</td>
      <td>${formatPoint(stream.center_mm)}</td>
      <td>${formatMm(stream.center_disagreement_mm)}</td>
      <td>${formatMm(stream.corner_disagreement_rmse_mm)}</td>
      <td>${(stream.edge_lengths_mm || []).map((value) => Number(value).toFixed(2)).join(' / ')} mm</td>
      <td>${Number(stream.orientation_deg).toFixed(2)}°</td>
    </tr>`).join('');
    return `<tr>
      <td><strong>ID ${escapeHtml(marker.id)}</strong></td>
      <td>${escapeHtml(streams.map((stream) => stream.stream_id).join(', '))}</td>
      <td>${formatMm(centerMax)}</td>
      <td>${formatMm(marker.corner_disagreement_rmse_mm)}</td>
      <td>${formatMm(marker.max_corner_disagreement_mm)}</td>
      <td><details><summary>채널별 수치 보기</summary>
        <table class="verification-detail-table">
          <thead><tr><th>스트림</th><th>중심 좌표(mm)</th><th>중심 불일치</th><th>꼭짓점 불일치</th><th>변 길이(mm)</th><th>방향</th></tr></thead>
          <tbody>${detailRows}</tbody>
        </table>
      </details></td>
    </tr>`;
  }).join('') || '<tr><td colspan="6">공통 마커 수치가 없습니다.</td></tr>';

  const heldOutCheck = payload.held_out_overlap_check;
  const crossBox = $('#verification-cross-validation');
  if (!crossBox) return;
  if (!heldOutCheck) {
    crossBox.innerHTML = `
      <h3>겹침 마커 하나 제외 확인</h3>
      <p class="verification-note">서버 응답에 제외 확인 값이 없습니다. 다시 겹침 구간 연결을 실행하세요.</p>`;
  } else if (!heldOutCheck.available) {
    crossBox.innerHTML = `
      <h3>겹침 마커 하나 제외 확인</h3>
      <div class="verification-cross-unavailable">
        <strong>제외 확인을 계산할 수 없습니다.</strong>
        <span>${escapeHtml(heldOutCheck.reason || '공통 마커가 부족합니다.')}</span>
      </div>
      <p class="verification-note">공통 마커를 최소 4개 이상 확보해야 마커 하나를 제외하고 나머지로 예측할 수 있습니다.</p>`;
  } else {
    const edgeRows = (heldOutCheck.edges || []).map((edge) => {
      const heldOut = (edge.held_out || []).filter((item) => item.available);
      const values = heldOut.length
        ? heldOut.map((item) =>
          `ID ${escapeHtml(item.marker_id)} · 예측 오차 ${formatMm(item.prediction_rmse_mm)} · 최대 ${formatMm(item.max_prediction_error_mm)}`
        ).join('<br>')
        : escapeHtml(edge.reason || '계산된 제외 마커가 없습니다.');
      return `<tr>
        <td>${escapeHtml((edge.stream_ids || []).join(' ↔ '))}</td>
        <td>${escapeHtml((edge.common_marker_ids || []).join(', '))}</td>
        <td>${formatMm(edge.prediction_rmse_mm)}</td>
        <td>${formatMm(edge.max_prediction_error_mm)}</td>
        <td>${values}</td>
      </tr>`;
    }).join('');
    crossBox.innerHTML = `
      <div class="verification-cross-heading">
        <div>
          <h3>겹침 마커 하나 제외 확인</h3>
          <p>공통 마커를 하나씩 숨기고, 나머지 마커만으로 숨긴 마커 위치를 다시 예측했습니다.</p>
        </div>
        <span class="verification-cross-badge">겹침 마커 하나 제외 확인</span>
      </div>
      <div class="verification-cross-summary">
        <div>숨긴 겹침 마커 예측 오차<strong>${formatMm(heldOutCheck.prediction_rmse_mm)}</strong></div>
        <div>최대 예측 오차<strong>${formatMm(heldOutCheck.max_prediction_error_mm)}</strong></div>
        <div>확인한 마커-연결<strong>${escapeHtml(heldOutCheck.tested_case_count)}건</strong></div>
      </div>
      <div class="verification-table-wrap">
        <table class="verification-table verification-cross-table">
          <thead><tr>
            <th>스트림 연결</th><th>공통 마커</th><th>숨긴 겹침 마커 예측 오차</th>
            <th>최대 예측 오차</th><th>제외 마커별 상세</th>
          </tr></thead>
          <tbody>${edgeRows || '<tr><td colspan="5">제외 확인 상세가 없습니다.</td></tr>'}</tbody>
        </table>
      </div>
      <p class="verification-note">
        이 값은 마커가 놓인 위치를 일부러 제외해 얻은 일반화 오차입니다.
        마커가 전혀 없는 맵 바깥 영역의 실제 오차를 보증하는 값은 아니며,
        진짜 전체 맵 수치 검증에는 정합 계산에서 제외한 체크 마커를 영역 전체에 골고루 두어야 합니다.
      </p>`;
  }

  $('#verification-status').textContent =
    `수치 검증 완료 · 공통 마커 ${markers.length}개 · 적합 오차와 일반화 오차를 구분해 확인하세요.`;
}

function buildClientVerificationMarkers(payload, streams) {
  const grouped = new Map();
  streams.forEach((stream) => {
    // verification.streams의 H는 이미 합성된 카메라 픽셀→공유 지도 H다.
    const globalH = stream.H_camera_pixels_to_shared_map;
    Object.entries(stream.markers || {}).forEach(([markerId, corners]) => {
      const worldCorners = corners.map((corner) => {
        const global = homographyWorldPoint(globalH, corner.x, corner.y);
        return {x: global.x / global.w, y: global.y / global.w};
      });
      const center = {
        x: worldCorners.reduce((sum, value) => sum + value.x, 0) / 4,
        y: worldCorners.reduce((sum, value) => sum + value.y, 0) / 4,
      };
      if (!grouped.has(markerId)) grouped.set(markerId, []);
      grouped.get(markerId).push({stream_id: stream.stream_id,
        corners: worldCorners, center});
    });
  });
  return [...grouped.entries()].filter(([, values]) => values.length >= 2)
    .map(([id, values]) => {
      const consensusCorners = [0, 1, 2, 3].map((corner) => ({
        x: values.reduce((sum, value) => sum + value.corners[corner].x, 0) / values.length,
        y: values.reduce((sum, value) => sum + value.corners[corner].y, 0) / values.length,
      }));
      const consensusCenter = {
        x: consensusCorners.reduce((sum, value) => sum + value.x, 0) / 4,
        y: consensusCorners.reduce((sum, value) => sum + value.y, 0) / 4,
      };
      let totalSquared = 0;
      let maximumError = 0;
      const detail = values.map((value) => {
        const cornerErrors = value.corners.map((actual, corner) => {
          const error = Math.hypot(actual.x - consensusCorners[corner].x,
            actual.y - consensusCorners[corner].y);
          totalSquared += error * error;
          maximumError = Math.max(maximumError, error);
          return error;
        });
        const edgeLengths = [0, 1, 2, 3].map((corner) => Math.hypot(
          value.corners[(corner + 1) % 4].x - value.corners[corner].x,
          value.corners[(corner + 1) % 4].y - value.corners[corner].y));
        return {
          stream_id: value.stream_id,
          center_mm: value.center,
          corner_errors_mm: cornerErrors,
          corner_rmse_mm: Math.sqrt(cornerErrors.reduce((sum, error) => sum + error * error, 0) / 4),
          edge_lengths_mm: edgeLengths,
          orientation_deg: Math.atan2(value.corners[1].y - value.corners[0].y,
            value.corners[1].x - value.corners[0].x) * 180 / Math.PI,
          center_error_mm: Math.hypot(value.center.x - consensusCenter.x,
            value.center.y - consensusCenter.y),
        };
      });
      return {
        id: Number(id),
        stream_count: values.length,
        consensus_center_mm: consensusCenter,
        consensus_corners_mm: consensusCorners,
        corner_rmse_mm: Math.sqrt(totalSquared / (values.length * 4)),
        max_corner_error_mm: maximumError,
        streams: detail,
      };
    });
}

async function prepareVerification(payload) {
  const verification = payload.verification;
  if (!verification || !verification.streams || !verification.common_markers) {
    throw new Error('서버가 공통 마커 검증 수치를 반환하지 않았습니다.');
  }
  let normalizedPayload = payload;
  const hasDetails = verification.common_markers.some((marker) =>
    Array.isArray(marker.streams));
  if (!hasDetails) {
    const streams = await Promise.all(Object.values(verification.streams).map(
      (stream) => loadCapturedMarkers(stream)));
    const detailedMarkers = buildClientVerificationMarkers(payload, streams);
    normalizedPayload = {
      ...payload,
      verification: {
        ...verification,
        streams: Object.fromEntries(streams.map((stream) => [stream.stream_id, stream])),
        common_markers: detailedMarkers,
      },
    };
  }
  state.verification = {...normalizedPayload.verification, mode: 'values'};
  $('#verification-panel').hidden = false;
  renderVerificationValues(normalizedPayload);
}

function setupVerificationPointerControls() {
  const target = $('#verification-viewport');
  target.addEventListener('wheel', (event) => {
    if (!state.verification) return;
    event.preventDefault();
    const rect = target.getBoundingClientRect();
    const cursorX = event.clientX - (rect.left + rect.width / 2);
    const cursorY = event.clientY - (rect.top + rect.height / 2);
    const factor = event.deltaY < 0 ? 1.18 : 0.85;
    const previousScale = state.verificationView.scale;
    const scale = Math.max(0.5, Math.min(8, previousScale * factor));
    const actualFactor = scale / previousScale;
    // 커서가 가리키는 지점은 그대로 두고 그 주변만 확대·축소한다.
    state.verificationView.x = cursorX - (cursorX - state.verificationView.x) * actualFactor;
    state.verificationView.y = cursorY - (cursorY - state.verificationView.y) * actualFactor;
    state.verificationView.scale = scale;
    applyVerificationView();
  }, {passive: false});

  target.addEventListener('pointerdown', (event) => {
    if (!state.verification || event.button !== 1) return;
    event.preventDefault();
    state.verificationPointer = {
      x: event.clientX,
      y: event.clientY,
      offsetX: state.verificationView.x,
      offsetY: state.verificationView.y,
    };
    target.setPointerCapture(event.pointerId);
  });
  target.addEventListener('pointermove', (event) => {
    if (!state.verificationPointer) return;
    event.preventDefault();
    state.verificationView.x = state.verificationPointer.offsetX +
      event.clientX - state.verificationPointer.x;
    state.verificationView.y = state.verificationPointer.offsetY +
      event.clientY - state.verificationPointer.y;
    applyVerificationView();
  });
  const release = (event) => {
    if (!state.verificationPointer) return;
    state.verificationPointer = null;
    try { target.releasePointerCapture(event.pointerId); } catch (_) {}
  };
  target.addEventListener('pointerup', release);
  target.addEventListener('pointercancel', release);
}

function setupPointerControls() {
  canvas.addEventListener('pointerdown', (event) => {
    event.preventDefault();
    const screen = screenPoint(event);
    if (event.button === 1) {
      state.pointer = {type: 'pan', screen, x: state.view.x, y: state.view.y};
      canvas.setPointerCapture(event.pointerId);
      return;
    }
    if (event.button !== 0 || !state.currentCapture) return;
    const nearest = nearestCorner(screen);
    if (!nearest) return;
    state.pointer = {type: 'corner', ...nearest};
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener('pointermove', (event) => {
    if (!state.pointer) return;
    event.preventDefault();
    const screen = screenPoint(event);
    if (state.pointer.type === 'pan') {
      state.view.x = state.pointer.x + screen.x - state.pointer.screen.x;
      state.view.y = state.pointer.y + screen.y - state.pointer.screen.y;
      drawCapture();
      return;
    }
    const capture = state.currentCapture;
    if (!capture) return;
    const corners = markerCorners(capture, state.pointer.markerIndex);
    corners[state.pointer.cornerIndex] = imagePoint(screen);
    capture.overrides[String(state.pointer.id)] = corners;
    capture.localResult = null;
    drawCapture();
    updateLocalResult();
    updateGlobalState();
  });
  const release = (event) => {
    if (!state.pointer) return;
    state.pointer = null;
    try { canvas.releasePointerCapture(event.pointerId); } catch (_) {}
  };
  canvas.addEventListener('pointerup', release);
  canvas.addEventListener('pointercancel', release);
  canvas.addEventListener('wheel', (event) => {
    event.preventDefault();
    const screen = screenPoint(event);
    const before = imagePoint(screen);
    const factor = event.deltaY < 0 ? 1.12 : 0.89;
    state.view.scale = Math.max(0.03, Math.min(12, state.view.scale * factor));
    state.view.x = screen.x - before.x * state.view.scale;
    state.view.y = screen.y - before.y * state.view.scale;
    drawCapture();
  }, {passive: false});
}

function setupEventHandlers() {
  $('#capture-camera-select').onchange = (event) => {
    state.selectedCameraId = event.target.value;
    state.selectedChannel = 1;
    populateChannelSelector();
    updateCameraSettingsFields();
  };
  $('#capture-channel-select').onchange = (event) => {
    state.selectedChannel = Number(event.target.value);
    showSelectedStream();
  };
  $('#camera-list').onchange = (event) => {
    state.selectedCameraId = event.target.value;
    $('#capture-camera-select').value = state.selectedCameraId;
    state.selectedChannel = 1;
    populateChannelSelector();
    updateCameraSettingsFields();
  };
  $('#capture-camera').onclick = captureSelected;
  $('#fit-view').onclick = fitCapture;
  $('#marker-size-mm').oninput = updateLocalResult;
  $('#solve-homography').onclick = solveLocal;
  $('#global-align-channels').onclick = alignAllStreams;
  $('#clear-log').onclick = () => { $('#result-log').textContent = '채널 캡처를 시작하세요.'; };
  $('#save-camera-settings').onclick = async () => {
    try {
      const result = await post('/api/camera/settings', {
        camera_id: $('#camera-id').value.trim(),
        camera_model: $('#camera-model').value,
      });
      $('#camera-settings-result').textContent = `${result.camera_id} 설정 저장 완료 · ${result.channel_count}채널`;
      await loadStatus();
    } catch (error) {
      $('#camera-settings-result').textContent = `설정 저장 실패: ${error.message}`;
    }
  };
  $('#marker-list').onclick = (event) => {
    const button = event.target.closest('[data-reference-marker]');
    if (!button || !state.currentCapture) return;
    state.currentCapture.referenceMarkerId = Number(button.dataset.referenceMarker);
    renderMarkerList();
    drawCapture();
    updateLocalResult();
  };
  $('#marker-list').onchange = (event) => {
    const checkbox = event.target.closest('[data-use-marker]');
    if (!checkbox || !state.currentCapture) return;
    const id = Number(checkbox.dataset.useMarker);
    if (checkbox.checked) state.currentCapture.excluded.delete(id);
    else state.currentCapture.excluded.add(id);
    ensureReferenceMarker(state.currentCapture);
    renderMarkerList();
    drawCapture();
    updateLocalResult();
  };
  window.addEventListener('resize', () => { resizeCanvas(); });
}

async function loadStatus() {
  const status = await getJson('/api/status');
  state.status = status;
  state.streams = status.streams || [];
  populateCameraSelectors();
  updateHeader();
  updateGlobalState();
}

setupEventHandlers();
setupPointerControls();
resizeCanvas();
loadStatus().catch((error) => {
  $('#camera-status').textContent = `설정 로드 실패: ${error.message}`;
  log(error.message);
});
