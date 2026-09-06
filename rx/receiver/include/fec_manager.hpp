#pragma once

#include <vector>
#include "uniflow.pb.h"

namespace fec {

bool init();

bool reconstruct_block(std::vector<uniflow::UniflowPacket>& block);

}
