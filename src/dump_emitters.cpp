#include "dump_emitters.hpp"

#include <functional>

#include "dump_writer_utils.hpp"

namespace husk::commands {

namespace {

// Shared "write one M2Track<T>'s fully-resolved curve" shape: interpolation
// type + global sequence, then either every non-empty M2Sequence's own
// keyframes (indexed 0..sequenceCount-1, skipping sequences with no data
// for this specific track -- most don't) or, for a global-sequence-driven
// track, its single independent curve. `writeValue` serializes one decoded
// keyframe value; `resolveSeq`/`resolveGlobal` are one of
// resolveVec3TrackSequence/resolveFloatTrackSequence/
// resolveRawIntTrackSequence and its GlobalSequenceTrack counterpart.
template <typename T, typename ResolveSeqFn, typename ResolveGlobalFn, typename WriteValueFn>
void writeTrackCurve(json::Writer& w, const std::vector<uint8_t>& blob, uint32_t trackOffset,
                      uint32_t sequenceCount, ResolveSeqFn resolveSeq, ResolveGlobalFn resolveGlobal,
                      WriteValueFn writeValue) {
    m2::TrackMeta meta = m2::readTrackMeta(blob, trackOffset);
    w.beginObject();
    w.key("interpolation_type");
    w.value(static_cast<int64_t>(meta.interpolationType));

    auto writeKeyframes = [&](const std::vector<std::pair<uint32_t, T>>& kfs) {
        w.beginArray();
        for (const auto& [ts, v] : kfs) {
            w.beginObject();
            w.key("time_ms");
            w.value(static_cast<int64_t>(ts));
            w.key("value");
            writeValue(w, v);
            w.endObject();
        }
        w.endArray();
    };

    if (meta.globalSequence == m2::TrackMeta::kNoGlobalSequence) {
        w.key("global_sequence");
        w.nullValue();
        w.key("sequences");
        w.beginArray();
        for (uint32_t seq = 0; seq < sequenceCount; ++seq) {
            auto kfs = resolveSeq(blob, trackOffset, seq, nullptr);
            if (kfs.empty()) continue;
            w.beginObject();
            w.key("sequence_index");
            w.value(static_cast<int64_t>(seq));
            w.key("keyframes");
            writeKeyframes(kfs);
            w.endObject();
        }
        w.endArray();
    } else {
        w.key("global_sequence");
        w.value(static_cast<int64_t>(meta.globalSequence));
        w.key("keyframes");
        writeKeyframes(resolveGlobal(blob, trackOffset, nullptr));
    }
    w.endObject();
}

void writeFloatTrack(json::Writer& w, const std::vector<uint8_t>& blob, uint32_t trackOffset,
                      uint32_t sequenceCount) {
    writeTrackCurve<float>(
        w, blob, trackOffset, sequenceCount, m2::resolveFloatTrackSequence,
        m2::resolveFloatGlobalSequenceTrack,
        [](json::Writer& w2, float v) { w2.value(static_cast<double>(v)); });
}

void writeVec3Track(json::Writer& w, const std::vector<uint8_t>& blob, uint32_t trackOffset,
                     uint32_t sequenceCount) {
    writeTrackCurve<m2::Vec3>(w, blob, trackOffset, sequenceCount, m2::resolveVec3TrackSequence,
                               m2::resolveVec3GlobalSequenceTrack,
                               [](json::Writer& w2, const m2::Vec3& v) { writeVec3(w2, v); });
}

// `elementSize` is 1 (uint8_t) or 2 (uint16_t/fixed16); `writeValue` decides
// how the raw zero-extended uint32_t gets serialized -- as-is for a plain
// index/flag (texSlotTrack/visibilityTrack/enabledIn), or scaled to a
// 0.0..1.0 float the same way readFixed16TrackValue does (alphaTrack).
void writeRawIntTrack(json::Writer& w, const std::vector<uint8_t>& blob, uint32_t trackOffset,
                       uint32_t sequenceCount, size_t elementSize,
                       const std::function<void(json::Writer&, uint32_t)>& writeValue) {
    auto resolveSeq = [elementSize](const std::vector<uint8_t>& b, uint32_t off, uint32_t seq,
                                     const std::vector<uint8_t>*) {
        return m2::resolveRawIntTrackSequence(b, off, seq, elementSize);
    };
    auto resolveGlobal = [elementSize](const std::vector<uint8_t>& b, uint32_t off,
                                        const std::vector<uint8_t>*) {
        return m2::resolveRawIntGlobalSequenceTrack(b, off, elementSize);
    };
    writeTrackCurve<uint32_t>(w, blob, trackOffset, sequenceCount, resolveSeq, resolveGlobal, writeValue);
}

template <typename T, typename WriteValueFn>
void writeFBlockCurve(json::Writer& w, const std::vector<std::pair<uint16_t, T>>& keyframes,
                       WriteValueFn writeValue) {
    w.beginArray();
    for (const auto& [ts, v] : keyframes) {
        w.beginObject();
        // Not milliseconds -- see m2::FBlockMeta's doc comment for why this
        // is exposed raw rather than guessing a 0.0..1.0 rescale.
        w.key("timestamp_raw_u16");
        w.value(static_cast<int64_t>(ts));
        w.key("value");
        writeValue(w, v);
        w.endObject();
    }
    w.endArray();
}

void writeRibbon(json::Writer& w, const std::vector<uint8_t>& blob, const m2::Ribbon& r,
                  uint32_t sequenceCount) {
    w.beginObject();
    w.key("ribbon_id");
    w.value(static_cast<int64_t>(r.ribbonId));
    w.key("bone");
    w.value(static_cast<int64_t>(r.boneIndex));
    w.key("position");
    writeVec3(w, r.position);
    w.key("texture_indices");
    w.beginArray();
    for (uint16_t v : r.textureIndices) w.value(static_cast<int64_t>(v));
    w.endArray();
    w.key("material_indices");
    w.beginArray();
    for (uint16_t v : r.materialIndices) w.value(static_cast<int64_t>(v));
    w.endArray();
    w.key("edges_per_second");
    w.value(static_cast<double>(r.edgesPerSecond));
    w.key("edge_lifetime");
    w.value(static_cast<double>(r.edgeLifetime));
    w.key("gravity");
    w.value(static_cast<double>(r.gravity));
    w.key("texture_rows");
    w.value(static_cast<int64_t>(r.textureRows));
    w.key("texture_cols");
    w.value(static_cast<int64_t>(r.textureCols));
    w.key("priority_plane");
    w.value(static_cast<int64_t>(r.priorityPlane));
    w.key("ribbon_color_index");
    w.value(static_cast<int64_t>(r.ribbonColorIndex));
    w.key("texture_transform_lookup_index");
    w.value(static_cast<int64_t>(r.textureTransformLookupIndex));

    w.key("color_track");
    writeVec3Track(w, blob, r.colorTrackOffset, sequenceCount);
    w.key("alpha_track");
    writeRawIntTrack(w, blob, r.alphaTrackOffset, sequenceCount, 2, writeFixed16AsFloat);
    w.key("height_above_track");
    writeFloatTrack(w, blob, r.heightAboveTrackOffset, sequenceCount);
    w.key("height_below_track");
    writeFloatTrack(w, blob, r.heightBelowTrackOffset, sequenceCount);
    w.key("tex_slot_track");
    writeRawIntTrack(w, blob, r.texSlotTrackOffset, sequenceCount, 2, writeRawIntAsIs);
    w.key("visibility_track");
    writeRawIntTrack(w, blob, r.visibilityTrackOffset, sequenceCount, 1, writeRawIntAsIs);
    w.endObject();
}

void writeParticle(json::Writer& w, const std::vector<uint8_t>& blob, const m2::ParticleEmitter& p,
                    uint32_t sequenceCount) {
    w.beginObject();
    w.key("particle_id");
    w.value(static_cast<int64_t>(p.particleId));
    w.key("flags");
    w.value(static_cast<int64_t>(p.flags));
    w.key("position");
    writeVec3(w, p.position);
    w.key("bone");
    w.value(static_cast<int64_t>(p.boneId));
    w.key("texture_id_raw");
    w.value(static_cast<int64_t>(p.textureId));
    w.key("particle_model_filename");
    w.value(p.particleModelFilename);
    w.key("child_emitters_model_filename");
    w.value(p.childEmittersModelFilename);
    w.key("blending_type");
    w.value(static_cast<int64_t>(p.blendingType));
    w.key("emitter_type");
    w.value(static_cast<int64_t>(p.emitterType));
    w.key("particle_color_index");
    w.value(static_cast<int64_t>(p.particleColorIndex));
    w.key("multi_tex_scale");
    w.beginArray();
    w.value(static_cast<double>(p.multiTexScale[0]));
    w.value(static_cast<double>(p.multiTexScale[1]));
    w.endArray();
    w.key("priority_plane");
    w.value(static_cast<int64_t>(p.priorityPlane));
    w.key("rows");
    w.value(static_cast<int64_t>(p.rows));
    w.key("columns");
    w.value(static_cast<int64_t>(p.columns));

    w.key("emission_speed_track");
    writeFloatTrack(w, blob, p.emissionSpeedTrackOffset, sequenceCount);
    w.key("speed_variation_track");
    writeFloatTrack(w, blob, p.speedVariationTrackOffset, sequenceCount);
    w.key("vertical_range_track");
    writeFloatTrack(w, blob, p.verticalRangeTrackOffset, sequenceCount);
    w.key("horizontal_range_track");
    writeFloatTrack(w, blob, p.horizontalRangeTrackOffset, sequenceCount);
    w.key("gravity_track");
    writeFloatTrack(w, blob, p.gravityTrackOffset, sequenceCount);
    w.key("lifespan_track");
    writeFloatTrack(w, blob, p.lifespanTrackOffset, sequenceCount);
    w.key("lifespan_variation");
    w.value(static_cast<double>(p.lifespanVariation));
    w.key("emission_rate_track");
    writeFloatTrack(w, blob, p.emissionRateTrackOffset, sequenceCount);
    w.key("emission_rate_variation");
    w.value(static_cast<double>(p.emissionRateVariation));
    w.key("emission_area_width_track");
    writeFloatTrack(w, blob, p.emissionAreaWidthTrackOffset, sequenceCount);
    w.key("emission_area_length_track");
    writeFloatTrack(w, blob, p.emissionAreaLengthTrackOffset, sequenceCount);
    w.key("z_source_track");
    writeFloatTrack(w, blob, p.zSourceTrackOffset, sequenceCount);

    w.key("color_track");
    writeFBlockCurve(w, m2::resolveFBlockVec3(blob, p.colorTrackBlockOffset),
                      [](json::Writer& w2, const m2::Vec3& v) { writeVec3(w2, v); });
    w.key("alpha_track");
    writeFBlockCurve(w, m2::resolveFBlockFixed16(blob, p.alphaTrackBlockOffset),
                      [](json::Writer& w2, float v) { w2.value(static_cast<double>(v)); });
    w.key("scale_track");
    writeFBlockCurve(w, m2::resolveFBlockVec2(blob, p.scaleTrackBlockOffset),
                      [](json::Writer& w2, const m2::Vec2& v) {
                          w2.beginObject();
                          w2.key("x");
                          w2.value(static_cast<double>(v.x));
                          w2.key("y");
                          w2.value(static_cast<double>(v.y));
                          w2.endObject();
                      });
    w.key("scale_vary");
    w.beginObject();
    w.key("x");
    w.value(static_cast<double>(p.scaleVary.x));
    w.key("y");
    w.value(static_cast<double>(p.scaleVary.y));
    w.endObject();
    w.key("head_uv_anim_track");
    writeFBlockCurve(w, m2::resolveFBlockUint16(blob, p.headUVAnimBlockOffset), writeRawIntAsIs);
    w.key("tail_uv_anim_track");
    writeFBlockCurve(w, m2::resolveFBlockUint16(blob, p.tailUVAnimBlockOffset), writeRawIntAsIs);

    w.key("tail_length");
    w.value(static_cast<double>(p.tailLength));
    w.key("twinkle_speed");
    w.value(static_cast<double>(p.twinkleSpeed));
    w.key("twinkle_percent");
    w.value(static_cast<double>(p.twinklePercent));
    w.key("twinkle_scale_min");
    w.value(static_cast<double>(p.twinkleScaleMin));
    w.key("twinkle_scale_max");
    w.value(static_cast<double>(p.twinkleScaleMax));
    w.key("inherit_velocity_scale");
    w.value(static_cast<double>(p.inheritVelocityScale));
    w.key("drag");
    w.value(static_cast<double>(p.drag));
    w.key("base_spin");
    w.value(static_cast<double>(p.baseSpin));
    w.key("base_spin_variation");
    w.value(static_cast<double>(p.baseSpinVariation));
    w.key("spin_speed");
    w.value(static_cast<double>(p.spinSpeed));
    w.key("spin_speed_variation");
    w.value(static_cast<double>(p.spinSpeedVariation));
    w.key("tumble_min");
    writeVec3(w, p.tumbleMin);
    w.key("tumble_max");
    writeVec3(w, p.tumbleMax);
    w.key("wind_vector");
    writeVec3(w, p.windVector);
    w.key("wind_time");
    w.value(static_cast<double>(p.windTime));
    w.key("follow_speed1");
    w.value(static_cast<double>(p.followSpeed1));
    w.key("follow_scale1");
    w.value(static_cast<double>(p.followScale1));
    w.key("follow_speed2");
    w.value(static_cast<double>(p.followSpeed2));
    w.key("follow_scale2");
    w.value(static_cast<double>(p.followScale2));
    w.key("spline_points");
    w.beginArray();
    for (const auto& pt : p.splinePoints) writeVec3(w, pt);
    w.endArray();
    w.key("enabled_in_track");
    writeRawIntTrack(w, blob, p.enabledInTrackOffset, sequenceCount, 1, writeRawIntAsIs);
    w.key("multi_tex_scroll_mid");
    w.beginArray();
    for (float v : p.multiTexScrollMid) w.value(static_cast<double>(v));
    w.endArray();
    w.key("multi_tex_scroll_range");
    w.beginArray();
    for (float v : p.multiTexScrollRange) w.value(static_cast<double>(v));
    w.endArray();
    w.endObject();
}

}  // namespace

void dumpEmitters(json::Writer& w, const std::vector<uint8_t>& blob, const m2::Header& header) {
    w.key("ribbon_emitters");
    w.beginArray();
    for (const auto& r : m2::parseRibbons(blob, header.ribbonEmitters)) {
        writeRibbon(w, blob, r, header.sequences.count);
    }
    w.endArray();

    w.key("particle_emitters");
    if (header.particleEmitters.count > 0 && header.version < m2::kMinVerifiedParticleVersion) {
        w.beginObject();
        w.key("note");
        w.value("version " + std::to_string(header.version) + " is below Cataclysm (" +
                std::to_string(m2::kMinVerifiedParticleVersion) +
                ") -- M2Particle's record shape is only documented and verified for Cata+; not "
                "parsed structurally to avoid a silent misread");
        w.key("count");
        w.value(static_cast<int64_t>(header.particleEmitters.count));
        w.endObject();
    } else {
        w.beginArray();
        for (const auto& p : m2::parseParticles(blob, header.particleEmitters)) {
            writeParticle(w, blob, p, header.sequences.count);
        }
        w.endArray();
    }
}

}  // namespace husk::commands
