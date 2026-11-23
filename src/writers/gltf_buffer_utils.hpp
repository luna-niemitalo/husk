#pragma once

#include <cstdint>
#include <vector>

#include <tiny_gltf.h>

// tinygltf buffer/accessor bookkeeping helpers for appending raw
// vertex/index/animation/image bytes into the one growing tinygltf::Buffer
// a .glb carries (gltf_lean.cpp).
namespace husk::gltf {

// glTF 2.0 requires an accessor's total byte offset (bufferView.byteOffset
// here, since every accessor below leaves its own byteOffset at 0) to be a
// multiple of its component type's size -- 4 bytes for the FLOAT/
// UNSIGNED_INT accessors this file emits (vertex attributes, indices,
// animation sampler in/out). `buffer.data` is one shared, growing byte
// array with every bufferView's offset simply wherever the previous append
// left off, so anything of a length that isn't itself a multiple of 4 --
// most concretely, an embedded PNG, whose byte length is essentially never a multiple of 4 -- would silently
// misalign every bufferView appended after it for the rest of the file
// otherwise. Zero-pad up to the next 4-byte boundary after every append
// (image bytes included, see the ad hoc appends that don't go through
// appendBufferView) so that can never happen.
inline void padTo4(tinygltf::Buffer& buffer) {
    while (buffer.data.size() % 4 != 0) {
        buffer.data.push_back(0);
    }
}

// Appends `data`'s raw bytes to `buffer` and returns a BufferView covering
// exactly that span. `target` is TINYGLTF_TARGET_ARRAY_BUFFER for vertex
// attributes, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER for indices, or 0 for
// data that isn't a vertex/index buffer at all (e.g. inverse bind matrices).
template <typename T>
int appendBufferView(tinygltf::Buffer& buffer, std::vector<tinygltf::BufferView>& views,
                      const std::vector<T>& data, int target) {
    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = buffer.data.size();
    view.byteLength = data.size() * sizeof(T);
    view.target = target;

    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    buffer.data.insert(buffer.data.end(), bytes, bytes + view.byteLength);
    padTo4(buffer);

    views.push_back(view);
    return static_cast<int>(views.size()) - 1;
}

}  // namespace husk::gltf
