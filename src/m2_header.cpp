#include "m2_header.hpp"

#include <cctype>
#include <unordered_map>

namespace husk::m2 {

std::vector<std::string> globalFlagNames(uint32_t flags) {
    static const std::pair<uint32_t, const char*> kNames[] = {
        {GlobalFlag::kTiltX, "tilt_x"},
        {GlobalFlag::kTiltY, "tilt_y"},
        {GlobalFlag::kUseTextureCombinerCombos, "use_texture_combiner_combos"},
        {GlobalFlag::kLoadPhysData, "load_phys_data"},
        {GlobalFlag::kUnk0x80, "unk_0x80"},
        {GlobalFlag::kCameraRelated, "camera_related"},
        {GlobalFlag::kNewParticleRecord, "new_particle_record"},
        {GlobalFlag::kUnk0x400, "unk_0x400"},
        {GlobalFlag::kTextureTransformsUseBoneSequences, "texture_transforms_use_bone_sequences"},
        {GlobalFlag::kUnk0x1000, "unk_0x1000"},
        {GlobalFlag::kChunkedAnimFiles, "chunked_anim_files"},
        {GlobalFlag::kUnk0x4000, "unk_0x4000"},
        {GlobalFlag::kUnk0x8000, "unk_0x8000"},
        {GlobalFlag::kUnk0x10000, "unk_0x10000"},
        {GlobalFlag::kUnk0x20000, "unk_0x20000"},
        {GlobalFlag::kUnk0x40000, "unk_0x40000"},
        {GlobalFlag::kUnk0x80000, "unk_0x80000"},
        {GlobalFlag::kUnk0x100000, "unk_0x100000"},
        {GlobalFlag::kUnk0x200000, "unk_0x200000"},
        {GlobalFlag::kUnk0x40000000, "unk_0x40000000"},
    };
    std::vector<std::string> names;
    for (const auto& [bit, name] : kNames) {
        if (flags & bit) {
            names.push_back(name);
        }
    }
    return names;
}

const char* billboardModeName(uint32_t flags) {
    if (flags & BoneFlag::kSphericalBillboard) return "spherical";
    if (flags & BoneFlag::kCylindricalBillboardLockX) return "cylindrical_lock_x";
    if (flags & BoneFlag::kCylindricalBillboardLockY) return "cylindrical_lock_y";
    if (flags & BoneFlag::kCylindricalBillboardLockZ) return "cylindrical_lock_z";
    return nullptr;
}

// wowdev.wiki M2#Key-Bone_Lookup's "Key Bone Names" table, transcribed in
// full (193 rows, IDs and names both, including the real gaps in the ID
// sequence -- e.g. 46/47/90-189/191-289/294/295 simply aren't in the
// wiki's own table, not an omission here).
const char* keyBoneName(int32_t keyBoneId) {
    static const std::unordered_map<int32_t, const char*> kNames = {
        {0, "ArmL"},
        {1, "ArmR"},
        {2, "ShoulderL"},
        {3, "ShoulderR"},
        {4, "SpineLow"},
        {5, "Waist"},
        {6, "Head"},
        {7, "Jaw"},
        {8, "IndexFingerR"},
        {9, "MiddleFingerR"},
        {10, "PinkyFingerR"},
        {11, "RingFingerR"},
        {12, "ThumbR"},
        {13, "IndexFingerL"},
        {14, "MiddleFingerL"},
        {15, "PinkyFingerL"},
        {16, "RingFingerL"},
        {17, "ThumbL"},
        {18, "$BTH"},
        {19, "$CSR"},
        {20, "$CSL"},
        {21, "_Breath"},
        {22, "_Name"},
        {23, "_NameMount"},
        {24, "$CHD"},
        {25, "$CCH"},
        {26, "Root"},
        {27, "Wheel1"},
        {28, "Wheel2"},
        {29, "Wheel3"},
        {30, "Wheel4"},
        {31, "Wheel5"},
        {32, "Wheel6"},
        {33, "Wheel7"},
        {34, "Wheel8"},
        {35, "FaceAttenuation"},
        {36, "EXP_C1_Cape1"},
        {37, "EXP_C1_Cape2"},
        {38, "EXP_C1_Cape3"},
        {39, "EXP_C1_Cape4"},
        {40, "EXP_C1_Cape5"},
        {41, "EXP_C1_Tail1"},
        {42, "EXP_C1_Tail2"},
        {43, "EXP_C1_LoinBk1"},
        {44, "EXP_C1_LoinBk2"},
        {45, "EXP_C1_LoinBk3"},
        {48, "EXP_C1_Spine2"},
        {49, "EXP_C1_Neck1"},
        {50, "EXP_C1_Neck2"},
        {51, "EXP_C1_Pelvis1"},
        {52, "Buckle"},
        {53, "Chest"},
        {54, "Main"},
        {55, "EXP_R1_Leg1Twist1"},
        {56, "EXP_L1_Leg1Twist1"},
        {57, "EXP_R1_Leg2Twist1"},
        {58, "EXP_L1_Leg2Twist1"},
        {59, "FootL"},
        {60, "FootR"},
        {61, "ElbowR"},
        {62, "ElbowL"},
        {63, "EXP_L1_Shield1"},
        {64, "HandR"},
        {65, "HandL"},
        {66, "WeaponR"},
        {67, "WeaponL"},
        {68, "SpellHandL"},
        {69, "SpellHandR"},
        {70, "EXP_R1_Leg1Twist3"},
        {71, "EXP_L1_Leg1Twist3"},
        {72, "EXP_R1_Arm1Twist2"},
        {73, "EXP_L1_Arm1Twist2"},
        {74, "EXP_R1_Arm1Twist3"},
        {75, "EXP_L1_Arm1Twist3"},
        {76, "EXP_R1_Arm2Twist2"},
        {77, "EXP_L1_Arm2Twist2"},
        {78, "EXP_R1_Arm2Twist3"},
        {79, "EXP_L1_Arm2Twist3"},
        {80, "ForearmR"},
        {81, "ForearmL"},
        {82, "EXP_R1_Arm1Twist1"},
        {83, "EXP_L1_Arm1Twist1"},
        {84, "EXP_R1_Arm2Twist1"},
        {85, "EXP_L1_Arm2Twist1"},
        {86, "EXP_R1_FingerClawA1"},
        {87, "EXP_R1_FingerClawB1"},
        {88, "EXP_L1_FingerClawA1"},
        {89, "EXP_L1_FingerClawB1"},
        {190, "_BackCloak"},
        {191, "face_hair_00_M_JNT"},
        {192, "face_beard_00_M_JNT"},
        {193, "face_cheek_02_L_SkinPoint"},
        {194, "face_cheek_02_R_SkinPoint"},
        {195, "face_eyeCornerIn_00_L_SkinPoint"},
        {196, "face_eyeCornerIn_00_R_SkinPoint"},
        {197, "face_eyeCornerOut_00_L_SkinPoint"},
        {198, "face_eyeCornerOut_00_R_SkinPoint"},
        {199, "face_eyebrow_00_L_SkinPoint"},
        {200, "face_eyebrow_00_M_SkinPoint"},
        {201, "face_eyebrow_00_R_SkinPoint"},
        {202, "face_eyebrow_01_L_SkinPoint"},
        {203, "face_eyebrow_01_R_SkinPoint"},
        {204, "face_eyebrow_02_L_SkinPoint"},
        {205, "face_eyebrow_02_R_SkinPoint"},
        {206, "face_eyebrow_03_L_SkinPoint"},
        {207, "face_eyebrow_03_R_SkinPoint"},
        {208, "face_eyelidBot_00_L_SkinPoint"},
        {209, "face_eyelidBot_00_R_SkinPoint"},
        {210, "face_eyelidBot_01_L_SkinPoint"},
        {211, "face_eyelidBot_01_R_SkinPoint"},
        {212, "face_eyelidBot_02_L_SkinPoint"},
        {213, "face_eyelidBot_02_R_SkinPoint"},
        {214, "face_eyelidTop_00_L_SkinPoint"},
        {215, "face_eyelidTop_00_R_SkinPoint"},
        {216, "face_eyelidTop_01_L_SkinPoint"},
        {217, "face_eyelidTop_01_R_SkinPoint"},
        {218, "face_eyelidTop_02_L_SkinPoint"},
        {219, "face_eyelidTop_02_R_SkinPoint"},
        {220, "face_noseBridge_00_L_SkinPoint"},
        {221, "face_noseBridge_00_R_SkinPoint"},
        {222, "face_overEye_00_L_SkinPoint"},
        {223, "face_overEye_00_R_SkinPoint"},
        {224, "face_overOuterEye_00_L_SkinPoint"},
        {225, "face_overOuterEye_00_R_SkinPoint"},
        {226, "face_underEye_00_L_SkinPoint"},
        {227, "face_underEye_00_R_SkinPoint"},
        {228, "face_cheekPuff_00_L_SkinPoint"},
        {229, "face_cheekPuff_00_R_SkinPoint"},
        {230, "face_cheek_00_L_SkinPoint"},
        {231, "face_cheek_00_R_SkinPoint"},
        {232, "face_cheek_01_L_SkinPoint"},
        {233, "face_cheek_01_R_SkinPoint"},
        {234, "face_chin_00_L_SkinPoint"},
        {235, "face_chin_00_M_SkinPoint"},
        {236, "face_chin_00_R_SkinPoint"},
        {237, "face_ear_00_L_SkinPoint"},
        {238, "face_ear_00_R_SkinPoint"},
        {239, "face_jaw_01_M_SkinPoint"},
        {240, "face_jowl_00_L_SkinPoint"},
        {241, "face_jowl_00_R_SkinPoint"},
        {242, "face_jowl_01_L_SkinPoint"},
        {243, "face_jowl_01_R_SkinPoint"},
        {244, "face_lipBotBase_00_M_SkinPoint"},
        {245, "face_lipTopBase_00_M_SkinPoint"},
        {246, "face_mouthCorner_00_L_SkinPoint"},
        {247, "face_mouthCorner_00_R_SkinPoint"},
        {248, "face_mouthCurlBot_00_M_SkinPoint"},
        {249, "face_mouthCurlTop_00_M_SkinPoint"},
        {250, "face_mouth_00_M_SkinPoint"},
        {251, "face_nasLab_00_L_SkinPoint"},
        {252, "face_nasLab_00_R_SkinPoint"},
        {253, "face_nasLab_01_L_SkinPoint"},
        {254, "face_nasLab_01_R_SkinPoint"},
        {255, "face_noseBase_00_M_SkinPoint"},
        {256, "face_sneerDriver_00_L_SkinPoint"},
        {257, "face_sneerDriver_00_R_SkinPoint"},
        {258, "face_sneerLower_00_L_SkinPoint"},
        {259, "face_sneerLower_00_R_SkinPoint"},
        {260, "face_sneer_00_L_SkinPoint"},
        {261, "face_sneer_00_R_SkinPoint"},
        {262, "face_teethBot_00_M_SkinPoint"},
        {263, "face_teethTop_00_M_SkinPoint"},
        {264, "face_tongue_00_M_SkinPoint"},
        {265, "root_main_00_M_SkinPoint"},
        {266, "spine_mainBendy_00_M_SkinPoint"},
        {267, "clavicle_main_00_L_SkinPoint"},
        {268, "arm_shoulderBendy_00_L_SkinPoint"},
        {269, "hand_main_00_L_JNT"},
        {270, "hand_index_00_L_SkinPoint"},
        {271, "hand_main_00_L_SkinPoint"},
        {272, "hand_ring_00_L_SkinPoint"},
        {273, "hand_pinky_00_L_SkinPoint"},
        {274, "hand_thumb_00_L_SkinPoint"},
        {275, "clavicle_main_00_R_SkinPoint"},
        {276, "arm_shoulderBendy_00_R_SkinPoint"},
        {277, "hand_main_00_R_JNT"},
        {278, "hand_main_00_R_SkinPoint"},
        {279, "hand_middle_00_R_SkinPoint"},
        {280, "hand_ring_00_R_SkinPoint"},
        {281, "hand_pinky_00_R_SkinPoint"},
        {282, "hand_thumb_00_R_SkinPoint"},
        {283, "head_main_00_M_SkinPoint"},
        {284, "face_jaw_00_M_SkinPoint"},
        {285, "EXP_L1_Eye1"},
        {286, "EXP_R1_Eye1"},
        {287, "EXP_L1_EyeLid1"},
        {288, "EXP_R1_EyeLid1"},
        {289, "EXP_L1_EyeLid2"},
        {290, "EXP_R1_EyeLid2"},
        {292, "EXP_L1_WingArm1Twist1"},
        {293, "EXP_R1_WingArm1Twist1"},
        {296, "waterfall_top_sound"},
        {297, "waterfall_bottom_sound"},
    };
    auto it = kNames.find(keyBoneId);
    return it != kNames.end() ? it->second : nullptr;
}

// wowdev.wiki M2#Attachments' "Attachment Lookup" table, transcribed
// directly. IDs 58/59 are real gaps in the wiki's own table (56/57/60 are
// documented; 58/59 simply aren't), not an omission here.
const char* attachmentTypeName(uint32_t id) {
    static const std::unordered_map<uint32_t, const char*> kNames = {
        {0, "Shield"},
        {1, "HandRight"},
        {2, "HandLeft"},
        {3, "ElbowRight"},
        {4, "ElbowLeft"},
        {5, "ShoulderRight"},
        {6, "ShoulderLeft"},
        {7, "KneeRight"},
        {8, "KneeLeft"},
        {9, "HipRight"},
        {10, "HipLeft"},
        {11, "Helm"},
        {12, "Back"},
        {13, "ShoulderFlapRight"},
        {14, "ShoulderFlapLeft"},
        {15, "ChestBloodFront"},
        {16, "ChestBloodBack"},
        {17, "Breath"},
        {18, "PlayerName"},
        {19, "Base"},
        {20, "Head"},
        {21, "SpellLeftHand"},
        {22, "SpellRightHand"},
        {23, "Special1"},
        {24, "Special2"},
        {25, "Special3"},
        {26, "SheathMainHand"},
        {27, "SheathOffHand"},
        {28, "SheathShield"},
        {29, "PlayerNameMounted"},
        {30, "LargeWeaponLeft"},
        {31, "LargeWeaponRight"},
        {32, "HipWeaponLeft"},
        {33, "HipWeaponRight"},
        {34, "Chest"},
        {35, "HandArrow"},
        {36, "Bullet"},
        {37, "SpellHandOmni"},
        {38, "SpellHandDirected"},
        {39, "VehicleSeat1"},
        {40, "VehicleSeat2"},
        {41, "VehicleSeat3"},
        {42, "VehicleSeat4"},
        {43, "VehicleSeat5"},
        {44, "VehicleSeat6"},
        {45, "VehicleSeat7"},
        {46, "VehicleSeat8"},
        {47, "LeftFoot"},
        {48, "RightFoot"},
        {49, "ShieldNoGlove"},
        {50, "SpineLow"},
        {51, "AlteredShoulderR"},
        {52, "AlteredShoulderL"},
        {53, "BeltBuckle"},
        {54, "SheathCrossbow"},
        {55, "HeadTop"},
        {56, "VirtualSpellDirected"},
        {57, "Backpack"},
        {60, "Unknown"},
    };
    auto it = kNames.find(id);
    return it != kNames.end() ? it->second : nullptr;
}

// wowdev.wiki M2#Events' "Possible Events" table, transcribed directly
// (the "what" column, trimmed to a short name). Deliberately excludes rows
// with no documented meaning at all -- $CHD ("probably does not exist?!"),
// $CVS/$KVS/$WWG ("not found"/"not seen"), and the non-'$'-prefixed
// DEST/POIN/WHEE/BOTT/TOP oddities the wiki itself says are "purpose
// unknown" -- inventing a name for those would be a guess this table
// exists specifically to avoid making.
const char* eventName(const std::string& identifier) {
    static const std::unordered_map<std::string, const char*> kExact = {
        {"$BMD", "BowMissleDestination"},
        {"$AIM", "ComputeMissileTrajectory"},
        {"$ALT", "DisplayTransition"},
        {"$BRT", "PlaySoundKit_birth"},
        {"$BTH", "Breath"},
        {"$BWP", "PlayRangedItemPull"},
        {"$BWR", "BowRelease"},
        {"$CAH", "AttackHold"},
        {"$CCH", "FishingString"},
        {"$CFM", "UpdateMountHeightOrOffset"},
        {"$CMA", "UpdateMountHeightOrOffset"},
        {"$CPP", "PlayCombatActionAnimKit"},
        {"$CSD", "PlayEmoteSound"},
        {"$CSL", "ReleaseMissilesLeft"},
        {"$CSR", "ReleaseMissilesRight"},
        {"$CSS", "PlayWeaponSwooshSound"},
        {"$CST", "ReleaseMissiles"},
        {"$DSE", "DestroyEmitter"},
        {"$DSL", "DoodadSoundLoop"},
        {"$DSO", "DoodadSoundOneShot"},
        {"$DTH", "DeathThud"},
        {"$EAC", "ObjectPackageStateEnter3"},
        {"$EDC", "ObjectPackageStateEnter5"},
        {"$EMV", "ObjectPackageStateEnter4"},
        {"$ESD", "PlayEmoteStateSound"},
        {"$EWT", "ObjectPackageStateEnter2"},
        {"$FDX", "PlayUnitSound_stand"},
        {"$FSD", "HandleFootfallAnimEvent"},
        {"$HIT", "PlayWoundAnimKit"},
        {"$SCD", "PlaySoundKit_spellCastDirected"},
        {"$SHK", "AddShake"},
        {"$SHL", "ExchangeSheathedWeaponLeft"},
        {"$SHR", "ExchangeSheathedWeaponRight"},
        {"$SMD", "PlaySoundKit_submerged"},
        {"$SMG", "PlaySoundKit_submerge"},
        {"$SND", "PlaySoundKit_custom"},
        {"$TRD", "HandleSpellEventSound"},
        {"$WGG", "PlayUnitSound_wingGlide"},
        {"$WLB", "WeaponLeftBot"},
        {"$WLT", "WeaponLeftTop"},
        {"$WNG", "PlayUnitSound_wingFlap"},
        {"$WRB", "WeaponRightBot"},
        {"$WRT", "WeaponRightTop"},
        {"$WTB", "BowBottom"},
        {"$WTT", "BowTop"},
    };
    auto exact = kExact.find(identifier);
    if (exact != kExact.end()) return exact->second;

    // Bracket-ranged codes (e.g. "$AH0".."$AH3", all documented as one
    // "$AH[0-3]" wiki row) -- every real range in the source table is a
    // single trailing digit, never multi-digit.
    if (identifier.empty() || !std::isdigit(static_cast<unsigned char>(identifier.back()))) {
        return nullptr;
    }
    static const std::unordered_map<std::string, const char*> kBracketPrefix = {
        {"$AH", "PlaySoundKit_customAttack"},
        {"$BL", "FootstepHitLeftBackwards"},
        {"$BR", "FootstepHitRightBackwards"},
        {"$FD", "PlayFidgetSound"},
        {"$FL", "FootstepHitLeft"},
        {"$FR", "FootstepHitRight"},
        {"$GC", "PlayAnimatedSoundCustom"},
        {"$GO", "PlayAnimatedSound"},
        {"$RL", "FootstepHitLeftRunning"},
        {"$RR", "FootstepHitRightRunning"},
        {"$SL", "FootstepHitLeftStop"},
        {"$SR", "FootstepHitRightStop"},
        {"$VG", "HandleBoneAnimGrabEvent"},
        {"$VT", "HandleBoneAnimThrowEvent"},
        {"$WL", "FootstepHitLeft"},
        {"$WR", "FootstepHitRight"},
    };
    auto bracketed = kBracketPrefix.find(identifier.substr(0, identifier.size() - 1));
    return bracketed != kBracketPrefix.end() ? bracketed->second : nullptr;
}

// wowdev.wiki M2#Textures' "Texture types" table, transcribed directly.
// IDs 24-26 are deliberately absent -- the wiki lists them as real (seen in
// DracthyrDragon.m2, 10.0.0+) but gives no name for any of the three.
const char* textureTypeName(uint32_t type) {
    static const std::unordered_map<uint32_t, const char*> kNames = {
        {1, "skin"},
        {2, "object_skin"},
        {3, "weapon_blade"},
        {4, "weapon_handle"},
        {5, "environment"},
        {6, "char_hair"},
        {7, "char_facial_hair"},
        {8, "skin_extra"},
        {9, "ui_skin"},
        {10, "tauren_mane"},
        {11, "monster_1"},
        {12, "monster_2"},
        {13, "monster_3"},
        {14, "item_icon"},
        {15, "guild_background_color"},
        {16, "guild_emblem_color"},
        {17, "guild_border_color"},
        {18, "guild_emblem"},
        {19, "char_eyes"},
        {20, "char_jewelry"},
        {21, "char_secondary_skin"},
        {22, "char_secondary_hair"},
        {23, "char_secondary_armor"},
    };
    auto it = kNames.find(type);
    return it != kNames.end() ? it->second : nullptr;
}

std::vector<Texture> parseTextures(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Texture> textures;
    if (array.count == 0) {
        return textures;
    }

    // M2Texture, wowdev.wiki M2#Textures: type(u32) + flags(u32) +
    // filename(M2Array<char>, 8 bytes) = 16 bytes.
    constexpr size_t kTextureSize = 0x10;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    // Same up-front validation as parseVertices/parseBones, same reason.
    // TODO: Remove: FAILURES.md #2.
    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kTextureSize) {
        throw ParseError("textures array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kTextureSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    textures.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kTextureSize;
        Texture t;
        t.type = readU32(data, blobSize, off + 0x00);
        t.flags = readU32(data, blobSize, off + 0x04);
        t.filename = readName(data, blobSize, readArray(data, blobSize, off + 0x08));
        textures.push_back(t);
    }

    return textures;
}

std::vector<Material> parseMaterials(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Material> materials;
    if (array.count == 0) {
        return materials;
    }

    constexpr size_t kMaterialSize = 0x04;  // M2Material: flags(u16) + blendMode(u16)
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kMaterialSize) {
        throw ParseError("materials array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kMaterialSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    materials.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kMaterialSize;
        Material m;
        m.flags = readU16(data, blobSize, off + 0x00);
        m.blendMode = readU16(data, blobSize, off + 0x02);
        materials.push_back(m);
    }

    return materials;
}

}  // namespace husk::m2
