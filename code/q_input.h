#pragma once

#define BUTTON_DOWN 1
#define BUTTION_PRESS 2 
#define BUTTION_RELEASE 4

struct Button
{
    I32 state;
};

struct InputSystem
{
    Button forward;
    Button side;
};
