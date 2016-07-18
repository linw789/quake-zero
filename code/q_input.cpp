#include "q_input.h"

InputSystem g_inputs;

void ButtonDown(Button *button)
{
    I32 is_down = button->state & BUTTON_DOWN;
    if (is_down)
    {
        return ;
    }
    button->state |= BUTTON_DOWN | BUTTON_PRESS;
}

void ButtonUp(Button *button)
{
    I32 is_down = button->state & BUTTON_DOWN;
    if (!is_down)
    {
        return ;
    }
    button->state &= ~BUTTON_DOWN;
    button->state |= BUTTON_RELEASE;
}

float EvaluateButtonState(Button *button)
{
    I32 action_press = button->state & BUTTON_PRESS;
    I32 action_release = button->state & BUTTON_RELEASE;
    I32 is_down = button->state & BUTTON_DOWN;

    float val = 0;
    
    // pressed the button this frame
    if (action_press && !action_release)
    {
        ASSERT(is_down);
        val = 0.5f;
    }
    // released the button this frame
    else if (!action_press && action_release)
    {
        ASSERT(!is_down);
        val = 0.0f;
    }
    else if (action_press && action_release)
    {
        if (is_down) // pressed then released
        {
            val = 0.25f;
        }
        else // released then pressed
        {
            val = .075f;
        }
    }
    else if (!action_press && !action_release)
    {
        if (is_down)
        {
            val = 1.0f
        }
    }

    return val;
}
