#include "render/mermaid_svg.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <map>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>

#include <tinyxml2.h>

#include "include/core/SkPathMeasure.h"
#include "include/utils/SkParsePath.h"

namespace mdviewer {
namespace {

using tinyxml2::XMLElement;

void FindMarkers(XMLElement* element, std::map<std::string, const XMLElement*>& markers) {
    if (std::string_view(element->Name()) == "marker") {
        if (const char* id = element->Attribute("id")) {
            markers[id] = element;
        }
    }
    for (auto* child = element->FirstChildElement(); child; child = child->NextSiblingElement()) {
        FindMarkers(child, markers);
    }
}

std::optional<SkPath> ElementPath(const XMLElement& element) {
    const std::string_view name(element.Name());
    if (name == "path") {
        const char* path = element.Attribute("d");
        return path ? SkParsePath::FromSVGString(path) : std::nullopt;
    }
    if (name == "polyline" || name == "polygon") {
        if (const char* points = element.Attribute("points")) {
            return SkParsePath::FromSVGString((std::string("M") + points + (name == "polygon" ? " Z" : "")).c_str());
        }
    }
    if (name == "line") {
        std::ostringstream path;
        path.imbue(std::locale::classic());
        path << "M " << element.FloatAttribute("x1") << ' ' << element.FloatAttribute("y1")
             << " L " << element.FloatAttribute("x2") << ' ' << element.FloatAttribute("y2");
        return SkParsePath::FromSVGString(path.str().c_str());
    }
    return std::nullopt;
}

bool ExpandMarker(XMLElement& element, const char* attribute, bool start,
    const std::map<std::string, const XMLElement*>& markers, std::string& error) {
    const char* value = element.Attribute(attribute);
    if (!value || std::string_view(value) == "none") {
        return true;
    }
    const std::string_view reference(value);
    if (!reference.starts_with("url(#") || !reference.ends_with(')')) {
        error = "Unsupported Mermaid marker reference.";
        return false;
    }
    const auto marker = markers.find(std::string(reference.substr(5, reference.size() - 6)));
    const auto path = ElementPath(element);
    if (marker == markers.end() || !path) {
        error = "Could not expand a Mermaid arrow marker.";
        return false;
    }
    SkPathMeasure measure(*path, false);
    SkPoint position;
    SkVector tangent;
    bool measured = false;
    do {
        if (measure.getLength() > 0.0f) {
            measured = measure.getPosTan(start ? 0.0f : measure.getLength(), &position, &tangent);
            if (start) break;
        }
    } while (measure.nextContour());
    if (!measured) {
        // A zero-length edge has no directional marker.
        element.DeleteAttribute(attribute);
        return true;
    }

    const auto& definition = *marker->second;
    float angle = std::atan2(tangent.y(), tangent.x()) * 180.0f / std::numbers::pi_v<float>;
    const std::string_view orient = definition.Attribute("orient") ? definition.Attribute("orient") : "0";
    if (orient == "auto-start-reverse") {
        if (start) angle += 180.0f;
    } else if (orient != "auto") {
        angle = definition.FloatAttribute("orient");
    }
    const float markerWidth = definition.FloatAttribute("markerWidth", 3.0f);
    const float markerHeight = definition.FloatAttribute("markerHeight", 3.0f);
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (const char* viewBox = definition.Attribute("viewBox")) {
        std::string numbers(viewBox);
        std::replace(numbers.begin(), numbers.end(), ',', ' ');
        std::istringstream stream(numbers);
        stream.imbue(std::locale::classic());
        float left = 0, top = 0, width = 0, height = 0;
        if (!(stream >> left >> top >> width >> height) || width <= 0.0f || height <= 0.0f) {
            error = "Invalid Mermaid marker viewBox.";
            return false;
        }
        scaleX = markerWidth / width;
        scaleY = markerHeight / height;
        const char* aspect = definition.Attribute("preserveAspectRatio");
        if (!aspect || std::string_view(aspect) != "none") {
            scaleX = scaleY = std::min(scaleX, scaleY);
        }
    }
    const char* units = definition.Attribute("markerUnits");
    const float strokeScale = units && std::string_view(units) == "userSpaceOnUse"
        ? 1.0f : element.FloatAttribute("stroke-width", 1.0f);
    std::ostringstream transform;
    transform.imbue(std::locale::classic());
    transform.precision(9);
    if (const char* original = element.Attribute("transform")) transform << original << ' ';
    transform << "translate(" << position.x() << ' ' << position.y() << ") rotate(" << angle
              << ") scale(" << scaleX * strokeScale << ' ' << scaleY * strokeScale
              << ") translate(" << -definition.FloatAttribute("refX") << ' '
              << -definition.FloatAttribute("refY") << ')';
    auto* group = element.GetDocument()->NewElement("g");
    // Marker children inherit presentation attributes from their definition,
    // not from the line that references it.
    for (const char* name : {"fill", "stroke", "stroke-width", "opacity", "style"}) {
        if (const char* inherited = definition.Attribute(name)) group->SetAttribute(name, inherited);
    }
    group->SetAttribute("transform", transform.str().c_str());
    for (auto* child = definition.FirstChild(); child; child = child->NextSibling()) {
        group->InsertEndChild(child->DeepClone(element.GetDocument()));
    }
    element.Parent()->InsertAfterChild(&element, group);
    element.DeleteAttribute(attribute);
    return true;
}

bool NormalizeElements(XMLElement* element,
    const std::map<std::string, const XMLElement*>& markers, std::string& error) {
    const std::string_view name(element->Name());
    if (name == "defs") return true;
    if (name == "foreignObject" || name == "image" || name == "style") {
        error = "This diagram requires SVG features outside the native viewer's supported subset.";
        return false;
    }
    if (name == "a") element->SetName("g"); // Diagram links are intentionally passive.
    if (element->Attribute("marker-mid")) {
        error = "Mid-edge Mermaid markers are not supported.";
        return false;
    }
    if (!ExpandMarker(*element, "marker-start", true, markers, error) ||
        !ExpandMarker(*element, "marker-end", false, markers, error)) return false;
    for (auto* child = element->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (!NormalizeElements(child, markers, error)) return false;
    }
    return true;
}

} // namespace

bool NormalizeMermaidSvg(std::string_view source, std::string& normalized, std::string& error,
    float intrinsicWidth, float intrinsicHeight) {
    tinyxml2::XMLDocument document;
    if (document.Parse(source.data(), source.size()) != tinyxml2::XML_SUCCESS || !document.RootElement()) {
        error = "The Mermaid renderer returned invalid SVG.";
        return false;
    }
    std::map<std::string, const XMLElement*> markers;
    FindMarkers(document.RootElement(), markers);
    if (!NormalizeElements(document.RootElement(), markers, error)) return false;
    if (std::isfinite(intrinsicWidth) && std::isfinite(intrinsicHeight) &&
        intrinsicWidth > 0.0f && intrinsicHeight > 0.0f) {
        // Standalone exports must not depend on a containing HTML viewport.
        document.RootElement()->SetAttribute("width", intrinsicWidth);
        document.RootElement()->SetAttribute("height", intrinsicHeight);
    }
    tinyxml2::XMLPrinter printer(nullptr, true);
    document.Print(&printer);
    normalized.assign(printer.CStr(), printer.CStrSize() - 1);
    return true;
}

} // namespace mdviewer
