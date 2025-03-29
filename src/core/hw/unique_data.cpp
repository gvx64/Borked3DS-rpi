// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cryptopp/sha.h>
#include "common/common_paths.h"
#include "common/logging/log.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/otp.h"
#include "core/hw/aes/key.h"
#include "core/hw/ecc.h"
#include "core/hw/rsa/rsa.h"
#include "core/hw/unique_data.h"
#include "core/loader/loader.h"

namespace HW::UniqueData {

static SecureInfoA secure_info_a;
static bool secure_info_a_signature_valid = false;
static bool secure_info_a_loaded = false;
static bool local_friend_code_seed_b_loaded = false;
static LocalFriendCodeSeedB local_friend_code_seed_b;
static bool local_friend_code_seed_b_signature_valid = false;
static FileSys::OTP otp;
static FileSys::Certificate ct_cert;
static MovableSedFull movable;
static bool movable_signature_valid = false;

bool SecureInfoA::VerifySignature() const {
    return HW::RSA::GetSecureInfoSlot().Verify(
        std::span<const u8>(reinterpret_cast<const u8*>(&body), sizeof(body)), signature);
}

bool LocalFriendCodeSeedB::VerifySignature() const {
    return HW::RSA::GetLocalFriendCodeSeedSlot().Verify(
        std::span<const u8>(reinterpret_cast<const u8*>(&body), sizeof(body)), signature);
}

bool MovableSed::VerifySignature() const {
    return lfcs.VerifySignature();
}

SecureDataLoadStatus LoadSecureInfoA() {
    if (secure_info_a.IsValid()) {
        return secure_info_a_signature_valid ? SecureDataLoadStatus::Loaded
                                             : SecureDataLoadStatus::InvalidSignature;
    }
    std::string file_path = GetSecureInfoAPath();
    if (!FileUtil::Exists(file_path)) {
        return SecureDataLoadStatus::NotFound;
    }
    FileUtil::IOFile file(file_path, "rb");
    if (!file.IsOpen()) {
        return SecureDataLoadStatus::IOError;
    }
    if (file.GetSize() != sizeof(SecureInfoA)) {
        return SecureDataLoadStatus::Invalid;
    }
    if (file.ReadBytes(&secure_info_a, sizeof(SecureInfoA)) != sizeof(SecureInfoA)) {
        secure_info_a.Invalidate();
        return SecureDataLoadStatus::IOError;
    }

    secure_info_a_signature_valid = secure_info_a.VerifySignature();
    if (!secure_info_a_signature_valid) {
        LOG_WARNING(HW, "SecureInfo_A signature check failed");
    }

    return secure_info_a_signature_valid ? SecureDataLoadStatus::Loaded
                                         : SecureDataLoadStatus::InvalidSignature;
}

SecureDataLoadStatus LoadLocalFriendCodeSeedB() {
    if (local_friend_code_seed_b.IsValid()) {
        return local_friend_code_seed_b_signature_valid ? SecureDataLoadStatus::Loaded
                                                        : SecureDataLoadStatus::InvalidSignature;
    }
    std::string file_path = GetLocalFriendCodeSeedBPath();
    if (!FileUtil::Exists(file_path)) {
        return SecureDataLoadStatus::NotFound;
    }
    FileUtil::IOFile file(file_path, "rb");
    if (!file.IsOpen()) {
        return SecureDataLoadStatus::IOError;
    }
    if (file.GetSize() != sizeof(LocalFriendCodeSeedB)) {
        return SecureDataLoadStatus::Invalid;
    }
    if (file.ReadBytes(&local_friend_code_seed_b, sizeof(LocalFriendCodeSeedB)) !=
        sizeof(LocalFriendCodeSeedB)) {
        local_friend_code_seed_b.Invalidate();
        return SecureDataLoadStatus::IOError;
    }

    local_friend_code_seed_b_signature_valid = local_friend_code_seed_b.VerifySignature();
    if (!local_friend_code_seed_b_signature_valid) {
        LOG_WARNING(HW, "LocalFriendCodeSeed_B signature check failed");
    }

    return local_friend_code_seed_b_signature_valid ? SecureDataLoadStatus::Loaded
                                                    : SecureDataLoadStatus::InvalidSignature;
}

SecureDataLoadStatus LoadOTP() {
    if (otp.Valid()) {
        return SecureDataLoadStatus::Loaded;
    }

    const std::string filepath = GetOTPPath();

    HW::AES::InitKeys();
    auto otp_keyiv = HW::AES::GetOTPKeyIV();

    auto loader_status = otp.Load(filepath, otp_keyiv.first, otp_keyiv.second);
    if (loader_status != Loader::ResultStatus::Success) {
        otp.Invalidate();
        ct_cert.Invalidate();
        return loader_status == Loader::ResultStatus::ErrorNotFound ? SecureDataLoadStatus::NotFound
                                                                    : SecureDataLoadStatus::Invalid;
    }

    constexpr const char* issuer_ret = "Nintendo CA - G3_NintendoCTR2prod";
    constexpr const char* issuer_dev = "Nintendo CA - G3_NintendoCTR2dev";
    std::array<u8, 0x40> issuer = {0};
    if (otp.IsDev()) {
        memcpy(issuer.data(), issuer_dev, strlen(issuer_dev));
    } else {
        memcpy(issuer.data(), issuer_ret, strlen(issuer_ret));
    }
    std::string name_str = fmt::format("CT{:08X}-{:02X}", otp.GetDeviceID(), otp.GetSystemType());
    std::array<u8, 0x40> name = {0};
    memcpy(name.data(), name_str.data(), name_str.size());

    ct_cert.BuildECC(issuer, name, otp.GetCTCertExpiration(),
                     HW::ECC::CreateECCPrivateKey(otp.GetCTCertPrivateKey(), true),
                     HW::ECC::CreateECCSignature(otp.GetCTCertSignature()));

    if (!ct_cert.VerifyMyself(HW::ECC::GetRootPublicKey())) {
        LOG_ERROR(HW, "CTCert failed verification");
        otp.Invalidate();
        ct_cert.Invalidate();
        return SecureDataLoadStatus::IOError;
    }

    return SecureDataLoadStatus::Loaded;
}

SecureDataLoadStatus LoadMovable() {
    if (movable.IsValid()) {
        return movable_signature_valid ? SecureDataLoadStatus::Loaded
                                       : SecureDataLoadStatus::InvalidSignature;
    }
    std::string file_path = GetMovablePath();
    if (!FileUtil::Exists(file_path)) {
        return SecureDataLoadStatus::NotFound;
    }
    FileUtil::IOFile file(file_path, "rb");
    if (!file.IsOpen()) {
        return SecureDataLoadStatus::IOError;
    }
    if (file.GetSize() != sizeof(MovableSedFull)) {
        if (file.GetSize() == sizeof(MovableSed)) {
            LOG_WARNING(HW, "Uninitialized movable.sed files are not supported");
        }
        return SecureDataLoadStatus::Invalid;
    }
    if (file.ReadBytes(&movable, sizeof(MovableSedFull)) != sizeof(MovableSedFull)) {
        movable.Invalidate();
        return SecureDataLoadStatus::IOError;
    }

    movable_signature_valid = movable.VerifySignature();
    if (!movable_signature_valid) {
        LOG_WARNING(HW, "LocalFriendCodeSeed_B signature check failed");
    }

    return movable_signature_valid ? SecureDataLoadStatus::Loaded
                                   : SecureDataLoadStatus::InvalidSignature;
}

SecureDataLoadStatus LoadSecureInfoAFile() {
    if (secure_info_a_loaded) {
        return SecureDataLoadStatus::Loaded;
    }
    std::string file_path = GetSecureInfoAPath();
    if (!FileUtil::Exists(file_path)) {
        return SecureDataLoadStatus::NotFound;
    }
    FileUtil::IOFile file(file_path, "rb");
    if (!file.IsOpen()) {
        return SecureDataLoadStatus::IOError;
    }
    if (file.GetSize() != sizeof(SecureInfoA)) {
        return SecureDataLoadStatus::Invalid;
    }
    if (file.ReadBytes(&secure_info_a, sizeof(SecureInfoA)) != sizeof(SecureInfoA)) {
        return SecureDataLoadStatus::IOError;
    }
    secure_info_a_loaded = true;
    return SecureDataLoadStatus::Loaded;
}

SecureDataLoadStatus LoadLocalFriendCodeSeedBFile() {
    if (local_friend_code_seed_b_loaded) {
        return SecureDataLoadStatus::Loaded;
    }
    std::string file_path = GetLocalFriendCodeSeedBPath();
    if (!FileUtil::Exists(file_path)) {
        return SecureDataLoadStatus::NotFound;
    }
    FileUtil::IOFile file(file_path, "rb");
    if (!file.IsOpen()) {
        return SecureDataLoadStatus::IOError;
    }
    if (file.GetSize() != sizeof(LocalFriendCodeSeedB)) {
        return SecureDataLoadStatus::Invalid;
    }
    if (file.ReadBytes(&local_friend_code_seed_b, sizeof(LocalFriendCodeSeedB)) !=
        sizeof(LocalFriendCodeSeedB)) {
        return SecureDataLoadStatus::IOError;
    }
    local_friend_code_seed_b_loaded = true;
    return SecureDataLoadStatus::Loaded;
}

std::string GetSecureInfoAPath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "rw/sys/SecureInfo_A";
}

std::string GetLocalFriendCodeSeedBPath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "rw/sys/LocalFriendCodeSeed_B";
}

std::string GetOTPPath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + "otp.bin";
}

std::string GetMovablePath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "private/movable.sed";
}

SecureInfoA& GetSecureInfoA() {
    LoadSecureInfoA();

    return secure_info_a;
}

LocalFriendCodeSeedB& GetLocalFriendCodeSeedB() {
    LoadLocalFriendCodeSeedB();

    return local_friend_code_seed_b;
}

FileSys::Certificate& GetCTCert() {
    LoadOTP();

    return ct_cert;
}

FileSys::OTP& GetOTP() {
    LoadOTP();

    return otp;
}
MovableSedFull& GetMovableSed() {
    LoadMovable();

    return movable;
}
void InvalidateSecureData() {
    secure_info_a.Invalidate();
    local_friend_code_seed_b.Invalidate();
    otp.Invalidate();
    ct_cert.Invalidate();
    movable.Invalidate();
}

std::unique_ptr<FileUtil::IOFile> OpenUniqueCryptoFile(const std::string& filename,
                                                       const char openmode[], UniqueCryptoFileID id,
                                                       int flags) {
    LoadOTP();

    if (!ct_cert.IsValid() || !otp.Valid()) {
        return std::make_unique<FileUtil::IOFile>();
    }

    struct {
        ECC::PublicKey pkey;
        u32 device_id;
        u32 id;
    } hash_data;
    hash_data.pkey = ct_cert.GetPublicKeyECC();
    hash_data.device_id = otp.GetDeviceID();
    hash_data.id = static_cast<u32>(id);

    CryptoPP::SHA256 hash;
    u8 digest[CryptoPP::SHA256::DIGESTSIZE];
    hash.CalculateDigest(digest, reinterpret_cast<CryptoPP::byte*>(&hash_data), sizeof(hash_data));

    std::vector<u8> key(0x10);
    std::vector<u8> ctr(0x10);
    memcpy(key.data(), digest, 0x10);
    memcpy(ctr.data(), digest + 0x10, 12);

    return std::make_unique<FileUtil::CryptoIOFile>(filename, openmode, key, ctr, flags);
}

} // namespace HW::UniqueData
