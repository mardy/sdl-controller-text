// normal includes
#include <stdlib.h>
#include <time.h>
#ifdef NINTENDO_WII
#include <gccore.h>
#include <wiiuse/wpad.h>
#endif

// SDL includes
#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <vector>

extern char _binary_images_data_end[], _binary_images_data_start[];
extern char _binary_mario286_ttf_start[], _binary_mario286_ttf_end[];

// screen surface, the place where everything will get print onto
SDL_Surface *screen = NULL;

void init() {

    setenv("SDL_WII_JOYSTICK_SPLIT", "1", 1);
    // initialize SDL video. If there was an error SDL shows it on the screen
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
        SDL_Delay(5000);
        exit(EXIT_FAILURE);
    }

    // make sure SDL cleans up before exit
    atexit(SDL_Quit);
    SDL_ShowCursor(SDL_DISABLE);

    // create a new window
    screen = SDL_SetVideoMode(640, 480, 16, SDL_DOUBLEBUF);
    if ( !screen )
    {
        fprintf(stderr, "Unable to set video: %s\n", SDL_GetError());
        SDL_Delay( 5000 );
        exit(EXIT_FAILURE);
    }

    TTF_Init();
}

void cleanup(){

    // we have to quit SDL
    SDL_Quit();
    exit(EXIT_SUCCESS);
}

struct Images {
    Images() {
        surface =
            SDL_CreateRGBSurfaceFrom(_binary_images_data_start,
                                     256, 256,
                                     32, 256 * 4,
                                     0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
        SDL_SetAlpha(surface, SDL_SRCALPHA, 0);
    }

    int blit(int number, SDL_Surface *dest, int x, int y) {
        SDL_Rect rect {
            Sint16(number * 32) % 256,
            Sint16(number / 8) * 32,
            32, 32,
        };
        SDL_Rect destRect { Sint16(x), Sint16(y), 0, 0 };
        return SDL_BlitSurface(surface, &rect, dest, &destRect);

    }

    SDL_Surface *surface;
};

static SDL_Color UintToColor(Uint32 color)
{
	SDL_Color tempcol;
	tempcol.r = (color >> 16) & 0xFF;
	tempcol.g = (color >> 8) & 0xFF;
	tempcol.b = color & 0xFF;
	return tempcol;
}

struct Fonts {
    Fonts() {
        SDL_RWops* pFontMem = SDL_RWFromConstMem(_binary_mario286_ttf_start,
                                                 _binary_mario286_ttf_end - _binary_mario286_ttf_start);
        if (!pFontMem) {
            fprintf(stderr, "Could not read font\n");
        }

        // Load the font from the memory buffer
        m_font = TTF_OpenFontRW(pFontMem, 1, 16);
    }

    int blit(const char *text, SDL_Surface *dest, int x, int y) {
        Uint32 color = SDL_MapRGB(dest->format, 255, 128, 128);
        SDL_Surface *surface = TTF_RenderText_Solid(m_font, text, UintToColor(color));
        SDL_Rect rect { 0, 0, surface->w, surface->h };
        SDL_Rect destRect { Sint16(x - surface->w / 2), Sint16(y), 0, 0 };
        int rc = SDL_BlitSurface(surface, &rect, dest, &destRect);
        SDL_FreeSurface(surface);
        return rc;
    }

private:
    TTF_Font *m_font;
};

class Input {
public:
    int xAxis() const { return m_x; }
    int yAxis() const { return m_y; }
    int buttonCount() const { return m_buttons; }

    virtual bool update() = 0;
    virtual bool buttonIsPressed(int index) = 0;
    virtual std::string name() const = 0;

protected:
    int m_x;
    int m_y;
    int m_buttons;
};

class KeyboardInput: public Input {
public:
    KeyboardInput(Uint8 *state): m_state(state) {
        m_buttons = 10;
    }

    bool update() override {
        m_x = m_y = 0;
        if (m_state[SDLK_LEFT]) m_x -= SHRT_MAX;
        if (m_state[SDLK_RIGHT]) m_x += SHRT_MAX;
        if (m_state[SDLK_UP]) m_y -= SHRT_MAX;
        if (m_state[SDLK_DOWN]) m_y += SHRT_MAX;
        return true;
    }

    bool buttonIsPressed(int index) override {
        return m_state[SDLK_0 + index];
    }

    std::string name() const override { return "Keyboard"; }

private:
    Uint8 *m_state;
};

class JoystickInput: public Input {
public:
    JoystickInput(SDL_Joystick *joy): m_joy(joy) {
        m_buttons = SDL_JoystickNumButtons(joy);
    }

    bool update() override {
        m_x = m_y = 0;
        m_x += SDL_JoystickGetAxis(m_joy, 0);
        m_y += SDL_JoystickGetAxis(m_joy, 1);
        switch (SDL_JoystickGetHat(m_joy, 0)) {
        case SDL_HAT_LEFT: m_x -= SHRT_MAX; break;
        case SDL_HAT_RIGHT: m_x += SHRT_MAX; break;
        case SDL_HAT_UP: m_y -= SHRT_MAX; break;
        case SDL_HAT_DOWN: m_y += SHRT_MAX; break;
        case SDL_HAT_RIGHTUP: m_x += SHRT_MAX; m_y -= SHRT_MAX; break;
        case SDL_HAT_RIGHTDOWN: m_x += SHRT_MAX; m_y += SHRT_MAX; break;
        case SDL_HAT_LEFTUP: m_x -= SHRT_MAX; m_y -= SHRT_MAX; break;
        case SDL_HAT_LEFTDOWN: m_x -= SHRT_MAX; m_y += SHRT_MAX; break;
        }
        return true;
    }

    bool buttonIsPressed(int index) override {
        return SDL_JoystickGetButton(m_joy, index);
    }

    std::string name() const override {
        return SDL_JoystickName(SDL_JoystickIndex(m_joy));
    }

    SDL_Joystick *m_joy;
};

struct Player {
    Player(float x, float y, uint32_t color, Input *input):
        x(x), y(y),
        rect { Sint16(x), Sint16(y), 32, 32 },
        color(color),
        input(input)
    {}

    void move(float dx, float dy) {
        x += dx; y += dy;
        x = std::clamp(x, .0f, float(640 - 32));
        y = std::clamp(y, .0f, float(480 - 32));
        rect.x = std::floor(x); rect.y = std::floor(y);
    }

    float x, y;
    SDL_Rect rect;
    Uint32 color;
    Input *input;
};

int main(int argc, char** argv){
    // main function. Always starts first

    // to stop the while loop
    bool done = false;

    // start init() function
    init();

    // this will make the red background
    // the first argument says it must be placed on the screen
    // the third argument gives the color in RGB format. You can change it if you want
    SDL_FillRect(screen, 0, SDL_MapRGB(screen->format, 0, 0, 0));

    std::vector<Player> players;

    struct PlayerPreset {
        int x;
        int y;
        Uint32 color;
    };
    static const std::vector<PlayerPreset> player_presets = {
        { 100, 100, SDL_MapRGB(screen->format, 255, 0, 0), },
        { 200, 100, SDL_MapRGB(screen->format, 0, 255, 0), },
        { 300, 100, SDL_MapRGB(screen->format, 0, 0, 255), },
        { 400, 100, SDL_MapRGB(screen->format, 255, 255, 0), },
        { 500, 100, SDL_MapRGB(screen->format, 0, 255, 255), },
        { 600, 100, SDL_MapRGB(screen->format, 255, 0, 255), },

        { 100, 200, SDL_MapRGB(screen->format, 255, 128, 0), },
        { 200, 200, SDL_MapRGB(screen->format, 0, 255, 128), },
        { 300, 200, SDL_MapRGB(screen->format, 255, 0, 128), },
        { 400, 200, SDL_MapRGB(screen->format, 128, 255, 0), },
        { 500, 200, SDL_MapRGB(screen->format, 0, 128, 255), },
        { 600, 200, SDL_MapRGB(screen->format, 128, 0, 255), },

        { 100, 300, SDL_MapRGB(screen->format, 255, 128, 255), },
        { 200, 300, SDL_MapRGB(screen->format, 255, 255, 128), },
        { 300, 300, SDL_MapRGB(screen->format, 255, 255, 128), },
        { 400, 300, SDL_MapRGB(screen->format, 128, 255, 255), },
        { 500, 300, SDL_MapRGB(screen->format, 255, 128, 255), },
        { 600, 300, SDL_MapRGB(screen->format, 128, 255, 255), },
    };
    int numPlayers = 0;

    int numKeys = 0;
    Uint8 *keyState = SDL_GetKeyState(&numKeys);
    if (numKeys > 0) {
        const PlayerPreset &p = player_presets[numPlayers++];
        players.emplace_back(Player(
            p.x, p.y, p.color,
            new KeyboardInput(keyState)
        ));
    }

    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_Joystick *joystick = SDL_JoystickOpen(i);
        if (!joystick) continue;

        const PlayerPreset &p = player_presets[numPlayers++];
        players.emplace_back(Player(p.x, p.y, p.color,
                                    new JoystickInput(joystick)));
    }

    uint32_t lastTick = SDL_GetTicks();
    const int desiredFPS = 60;
    int msPerFrame = 1000 / desiredFPS;

    Images images;
    Fonts fonts;

    // this is the endless while loop until done = true
    while (!done) {
        SDL_PumpEvents();
        SDL_JoystickUpdate();

        if (keyState[SDLK_ESCAPE]) done = true;

#ifdef NINTENDO_WII
        // scans if a button was pressed
        WPAD_ScanPads();
        u32 held = WPAD_ButtonsHeld(0);

        // if the homebutton is pressed it will set done = true and it will fill the screen
        // with a black background
        if(held & WPAD_BUTTON_HOME){
            done=true;
            SDL_FillRect(screen, 0, SDL_MapRGB(screen->format, 0, 0, 0));
        }
#endif

        uint32_t newTick = SDL_GetTicks();
        int32_t elapsed = newTick - lastTick;
        lastTick = newTick;
        SDL_FillRect(screen, 0, SDL_MapRGB(screen->format, 0, 0, 0));
        for (Player &player: players) {
            player.input->update();
            player.move(player.input->xAxis() * elapsed / 50000.0,
                        player.input->yAxis() * elapsed / 50000.0);
            SDL_FillRect(screen, &player.rect, player.color);
            for (int i = 0; i < player.input->buttonCount(); i++) {
                if (player.input->buttonIsPressed(i)) {
                    images.blit(i, screen, player.rect.x, player.rect.y);
                }
            }
            fonts.blit(player.input->name().c_str(), screen,
                       player.rect.x + player.rect.w / 2,
                       player.rect.y + player.rect.h);
        }
        // SDL_Flip refreshes the screen so it will show the updated screen
        SDL_Flip(screen);

        int32_t remaining = msPerFrame - elapsed;
        if (remaining > 0) {
            SDL_Delay(remaining);
        }
    }

    cleanup();

    return 0;
}
