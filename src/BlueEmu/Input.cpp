#include "Input.h"

Input::Input(): controller1(0), controller2(0), controller1_stream(0), controller2_stream(0) {

}

// -------------------------------
// Open first available controller
// -------------------------------
void Input::OpenFirstController() {
	controllers.clear();
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
			SDL_GameController* controller = SDL_GameControllerOpen(i);
			controllers.push_back(controller);
        }
    }
}

// -------------------------------
// Close controller
// -------------------------------
void Input::CloseController()
{
	for (int i = 0; i < controllers.size(); i++) {
		if (controllers[i]) {
			SDL_GameControllerClose(controllers[i]);
			controllers[i] = nullptr;
			//controllerInstanceID = -1;
		}
	}
	controllers.clear();
}

void Input::PollControllerState()
{
    controller1 = 0;

    const Uint8* keys = SDL_GetKeyboardState(NULL);
	// Map keyboard keys to controller buttons
    if (keys[SDL_SCANCODE_UP]) {
		controller1 |= BUTTON_UP;
    }
	if (keys[SDL_SCANCODE_DOWN]) {
		controller1 |= BUTTON_DOWN;
	}
	if (keys[SDL_SCANCODE_LEFT]) {
		controller1 |= BUTTON_LEFT;
	}
	if (keys[SDL_SCANCODE_RIGHT]) {
		controller1 |= BUTTON_RIGHT;
	}
	if (keys[SDL_SCANCODE_S]) {
		controller1 |= BUTTON_A;
	}
	if (keys[SDL_SCANCODE_A]) {
		controller1 |= BUTTON_B;
	}
	if (keys[SDL_SCANCODE_X]) {
		controller1 |= BUTTON_START;
	}
	if (keys[SDL_SCANCODE_Z]) {
		controller1 |= BUTTON_SELECT;
	}

	for (int i = 0; i < controllers.size(); i++) {
		SDL_GameController* controller = controllers[i];
		if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B)) {
			controller1 |= BUTTON_A;
		}
		if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) {
			controller1 |= BUTTON_B;
		}
		if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START)) {
			controller1 |= BUTTON_START;
		}
		if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK)) {
			controller1 |= BUTTON_SELECT;
		}

		if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
			controller1 |= BUTTON_UP;
		}
		if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
			controller1 |= BUTTON_DOWN;
		}
		if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
			controller1 |= BUTTON_LEFT;
		}
		if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
			controller1 |= BUTTON_RIGHT;
		}

		if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) < -8000) {
			controller1 |= BUTTON_LEFT;
		}
		if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) > 8000) {
			controller1 |= BUTTON_RIGHT;
		}
		if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) < -8000) {
			controller1 |= BUTTON_UP;
		}
		if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) > 8000) {
			controller1 |= BUTTON_DOWN;
		}
	}
}

void Input::ButtonDown(int key) {
	controller1 |= key;
	// Add cases for controller2 if needed
}

void Input::ButtonUp(int key) {
	controller1 &= ~key;
	// Add cases for controller2 if needed
}

void Input::Poll() {
	controller1_stream = controller1;
	controller2_stream = controller2;
}

uint8_t Input::ReadController1() {
	uint8_t ret = controller1_stream & 1;
	controller1_stream >>= 1;
	return ret;
}

uint8_t Input::ReadController2() {
	uint8_t ret = controller2_stream & 1;
	controller2_stream >>= 1;
	return ret;
}