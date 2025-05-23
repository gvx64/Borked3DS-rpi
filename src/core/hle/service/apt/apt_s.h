// Copyright 2015 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/apt/apt.h"

namespace Service::APT {

// Application and title launching service. These services handle signaling for home/power button as
// well. Only one session for either APT service can be open at a time, normally processes close the
// service handle immediately once finished using the service. The commands for APT:U and APT:S are
// exactly the same, however certain commands are only accessible with APT:S(NS module will call
// svcBreak when the command isn't accessible). See http://3dbrew.org/wiki/NS#APT_Services.

/// Interface to "APT:S" service
class APT_S final : public Module::APTInterface {
public:
    explicit APT_S(std::shared_ptr<Module> apt);

private:
    SERVICE_SERIALIZATION(APT_S, apt, Module)
};

} // namespace Service::APT

BOOST_CLASS_EXPORT_KEY(Service::APT::APT_S)
BOOST_SERIALIZATION_CONSTRUCT(Service::APT::APT_S)
