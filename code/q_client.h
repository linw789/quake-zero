#pragma once

#include "q_model.h"

// per-level limits
#define MAX_MODELS 256
#define MAX_SOUNDS 256

struct ClientState
{
    Model *precachedModels[MAX_MODELS];
};
