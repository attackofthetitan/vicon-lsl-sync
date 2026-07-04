#include "preview/ObjMesh.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vicon_lsl {
namespace {

unsigned int parseFaceIndex(const std::string& token, std::size_t vertex_count) {
    const std::size_t slash = token.find('/');
    const std::string index_text = slash == std::string::npos ? token : token.substr(0, slash);
    const int index = std::stoi(index_text);
    if (index > 0) {
        return static_cast<unsigned int>(index - 1);
    }
    const int resolved = static_cast<int>(vertex_count) + index;
    if (resolved < 0) {
        throw std::runtime_error("OBJ face index points before the vertex list");
    }
    return static_cast<unsigned int>(resolved);
}

} // namespace

PreviewMesh parseObjMesh(std::istream& input) {
    PreviewMesh mesh;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream line_stream(line);
        std::string tag;
        line_stream >> tag;
        if (tag == "v") {
            PreviewVec3 vertex;
            if (line_stream >> vertex.x >> vertex.y >> vertex.z) {
                mesh.vertices.push_back(vertex);
            }
        } else if (tag == "f") {
            std::vector<unsigned int> face;
            std::string token;
            while (line_stream >> token) {
                face.push_back(parseFaceIndex(token, mesh.vertices.size()));
            }
            if (face.size() >= 3) {
                mesh.faces.push_back(std::move(face));
            }
        }
    }

    if (mesh.vertices.empty() || mesh.faces.empty()) {
        throw std::runtime_error("OBJ mesh has no vertices or faces");
    }
    return mesh;
}

PreviewMesh loadObjMesh(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open OBJ mesh: " + path);
    }
    return parseObjMesh(input);
}

} // namespace vicon_lsl
