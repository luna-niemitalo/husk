#include "texture_payload.hpp"

#include "../blp.hpp"

namespace husk::sources {

canon::TextureRef::Payload ddsPayloadFromBlp(const std::vector<uint8_t>& blpBytes) {
    blp::RawPayload raw = blp::extractRawPayload(blpBytes);
    canon::TextureRef::Payload out;
    out.bytes = blp::encodeDds(raw);
    switch (raw.encoding) {
        case blp::RawEncoding::Bc1: out.encoding = canon::TextureEncoding::Bc1; break;
        case blp::RawEncoding::Bc2: out.encoding = canon::TextureEncoding::Bc2; break;
        case blp::RawEncoding::Bc3: out.encoding = canon::TextureEncoding::Bc3; break;
        case blp::RawEncoding::Bgra: out.encoding = canon::TextureEncoding::Bgra; break;
    }
    return out;
}

canon::TextureRef::Payload pngPayloadFromBlp(const std::vector<uint8_t>& blpBytes) {
    return canon::TextureRef::Payload{blp::encodePng(blp::decode(blpBytes)), canon::TextureEncoding::Png};
}

}  // namespace husk::sources
