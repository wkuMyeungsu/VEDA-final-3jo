// onvif_metadata_parser.cpp
#include "onvif_metadata_parser.hpp"
#include <pugixml.hpp>
#include <cstring>

// ONVIF XML은 실제 스트림에서 "tt:", "tns1:" 같은 네임스페이스 접두어가
// 문서마다 다르게 붙을 수 있어서, 접두어 무시하고 로컬 태그명만 비교
static bool matchesLocalName(const pugi::xml_node& node, const char* localName) {
    const char* fullName = node.name();
    const char* colon = std::strrchr(fullName, ':');
    const char* actual = colon ? colon + 1 : fullName;
    return std::strcmp(actual, localName) == 0;
}

static pugi::xml_node findChild(const pugi::xml_node& parent, const char* localName) {
    for (auto child : parent.children()) {
        if (matchesLocalName(child, localName)) return child;
    }
    return pugi::xml_node();
}

static float attrF(const pugi::xml_node& n, const char* name, float def = 0.f) {
    return n.attribute(name) ? n.attribute(name).as_float() : def;
}

static DetectedObject parseObject(const pugi::xml_node& objNode) {
    DetectedObject obj;
    obj.objectId = objNode.attribute("ObjectId").as_int(-1);
    obj.parentId = objNode.attribute("Parent").as_int(-1);

    pugi::xml_node appearance = findChild(objNode, "Appearance");
    if (!appearance) return obj;

    // --- Shape: BoundingBox + CenterOfGravity ---
    pugi::xml_node shape = findChild(appearance, "Shape");
    if (shape) {
        pugi::xml_node bboxNode = findChild(shape, "BoundingBox");
        if (bboxNode) {
            obj.bbox.left   = attrF(bboxNode, "left");
            obj.bbox.top    = attrF(bboxNode, "top");
            obj.bbox.right  = attrF(bboxNode, "right");
            obj.bbox.bottom = attrF(bboxNode, "bottom");
        }
        pugi::xml_node cogNode = findChild(shape, "CenterOfGravity");
        if (cogNode) {
            obj.centerOfGravity = { attrF(cogNode, "x"), attrF(cogNode, "y") };
        }
    }

    // --- Class: 최종 확정 클래스는 ClassCandidate 안이 아니라
    //     Class의 "직계 자식" Type에 있음 (실제 카메라 데이터로 확인됨)
    //     예: <tt:Class><tt:ClassCandidate>...</tt:ClassCandidate><tt:Type Likelihood="0.69">Human</tt:Type></tt:Class>
    pugi::xml_node classNode = findChild(appearance, "Class");
    if (classNode) {
        for (auto child : classNode.children()) {
            if (matchesLocalName(child, "Type")) {
            // ClassCandidate 내부의 Type은 건너뛰고, Class 바로 밑의 Type만 최종값으로 사용
            obj.classInfo.type = child.text().as_string();
            obj.classInfo.likelihood = attrF(child, "Likelihood");
        }
    }
}

    // --- ProximateObjects (거리 추정치 — 단위 확인 전이라 참고용) ---
    pugi::xml_node proxRoot = findChild(appearance, "ProximateObjects");
    if (proxRoot) {
        for (auto p : proxRoot.children()) {
            if (!matchesLocalName(p, "ProximateObject")) continue;
            ProximateObject po;
            po.id = p.attribute("Id").as_int(-1);
            po.distance = attrF(p, "Distance");
            obj.proximateObjects.push_back(po);
        }
    }

    return obj;
}

MetadataFrame parseOnvifMetadata(const std::string& xml) {
    MetadataFrame result;

    pugi::xml_document doc;
    pugi::xml_parse_result parseResult = doc.load_string(xml.c_str());
    if (!parseResult) {
        // 파싱 실패 시 빈 프레임 반환 — 호출부에서 로그 처리 권장
        return result;
    }

    pugi::xml_node root = doc.root().first_child(); // MetadataStream
    pugi::xml_node va = findChild(root, "VideoAnalytics");
    pugi::xml_node frame = va ? findChild(va, "Frame") : pugi::xml_node();
    if (!frame) return result;

    if (frame.attribute("UtcTime")) {
        result.utcTime = frame.attribute("UtcTime").as_string();
    }

    for (auto child : frame.children()) {
        if (matchesLocalName(child, "Object")) {
            result.objects.push_back(parseObject(child));
        }
    }
    return result;
}