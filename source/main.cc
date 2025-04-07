// 
// Shijima-Wii - Shimeji desktop pet runner for Nintendo Wii
// Copyright (C) 2025 pixelomer
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
// 

#include <exception>
#include <grrlib.h>
#include <memory>
#include <stdlib.h>
#include <cstdio>
#include <gccore.h>
#include <fat.h>
#include <unistd.h>
#include <wiiuse/wpad.h>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sstream>
#include <map>
#include <list>
#include <string>
#include <qutex/reader.hpp>
#include <shijima/shijima.hpp>
#include "font.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// eh
using namespace std;

static ostringstream consoleStream;
static vector<string> consoleLines;
static int firstLineIdx = 0;
static int realLineCount = 0;
static GRRLIB_texImg *texFont;
#define cerr consoleStream
#define cout consoleStream

#define MASCOT_LOCATION "/Shijima"

static void flushConsole() {
    string newConsole = consoleStream.str();
    if (!newConsole.empty()) {
        consoleStream = {};
        stringstream newStream { newConsole };
        string line;
        while (getline(newStream, line)) {
            if (realLineCount == (int)consoleLines.size()) {
                consoleLines[firstLineIdx] = line;
                firstLineIdx = (firstLineIdx + 1) % consoleLines.size();
            }
            else {
                consoleLines[realLineCount] = line;
                realLineCount++;
            }
        }
    }
}

static void drawConsole() {
    for (size_t i=firstLineIdx, j=0; j < consoleLines.size();
        i = (i+1) % consoleLines.size(), j++)
    {
        GRRLIB_Printf(0, + (j+1)*16, texFont, 0xFFFFFFFF,
            1, "%s", consoleLines[i].c_str());
    }
}

static void showConsoleNow() {
    flushConsole();
    drawConsole();
    GRRLIB_Render();
}

static bool fatalError = false;

static void die(string const& error) {
    cerr << "FATAL ERROR: " << error << endl;
    if (!fatalError) {
        cerr << "Shijima-Wii cannot continue. Press [HOME] to exit." << endl;
        fatalError = true;
    }
}

static void clearConsole() {
    for (auto &line : consoleLines) {
        line = "";
    }
    firstLineIdx = 0;
    realLineCount = 0;
    consoleStream = {};
}

static bool readFile(filesystem::path const& path, string &out) {
    stringstream buf;
    ifstream f { path, ios::binary };
    if (f.fail()) {
        return false;
    }
    buf << f.rdbuf();
    if (f.fail()) {
        return false;
    }
    out = buf.str();
    return true;
}

static void resized_sprite_write(void *context, void *data, int size) {
    auto &output = *(ostringstream *)context;
    output.write((const char *)data, (size_t)size);
}

class MascotSprite {
public:
    virtual void draw(f32 xpos, f32 ypos, bool flipX) const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual ~MascotSprite() {}
};

class MascotSpriteQutex : public MascotSprite {
public:
    MascotSpriteQutex(GRRLIB_texImg *tex, int cw, int ch, int xtex, int ytex,
        int wtex, int htex, int xoff, int yoff, int wreal, int hreal):
        tex(tex), cw(cw), ch(ch), xtex(xtex+1), ytex(ytex+1), wtex(wtex-1),
        htex(htex-1), xoff(xoff+1), yoff(yoff+1), wreal(wreal), hreal(hreal) {}
    virtual void draw(f32 xpos, f32 ypos, bool flipX) const {
        ypos += yoff;
        if (flipX) {
            xpos += cw - wreal;
        }
        else {
            xpos += xoff;
        }
        GRRLIB_DrawPart(xpos, ypos, xtex, ytex, wtex, htex,
            tex, 0, flipX ? -1 : 1, 1, 0xFFFFFFFF);
    }
    virtual int width() const {
        return wreal;
    }
    virtual int height() const {
        return hreal;
    }
    virtual ~MascotSpriteQutex() {}
private:
    GRRLIB_texImg *tex;
    int cw;
    int ch;
    int xtex;
    int ytex;
    int wtex;
    int htex;
    int xoff;
    int yoff;
    int wreal;
    int hreal;
};

//FIXME: no way to unload a texture ?
class MascotSpritePNG : public MascotSprite {
public:
    MascotSpritePNG(): m_valid(false) {}
    MascotSpritePNG(filesystem::path path): m_valid(false) {
        string data;
        if (!readFile(path, data)) {
            return;
        }
        int origWidth = -1, origHeight = -1, origComp = -1;
        int ok = stbi_info_from_memory((const u8 *)data.c_str(), data.size(),
            &origWidth, &origHeight, &origComp);
        if (ok != 1 || origWidth <= 0 || origHeight <= 0 || origComp <= 0) {
            return;
        }
        m_width = origWidth;
        m_height = origHeight;
        GRRLIB_texImg *tex = NULL;
        if (origWidth % 4 != 0 || origHeight % 4 != 0) {
            // attempt to resize first
            static bool firstResize = true;
            cerr << "WARNING: not mult of 4 -- " << path << endl;
            cerr << "WARNING: image size: " << origWidth << "x" << origHeight << endl;
            if (firstResize) {
                cerr << "Shijima-Wii will attempt to resize these images" << endl;
                cerr << "This is very slow, consider pre-packing with qutex" << endl;
                firstResize = false;
            }
            // the resize process is not too reliable
            // draw new console output to the screen now in case we crash
            showConsoleNow();
            static u8 buf[512 * 512 * 4];
            if (origWidth > 512 || origHeight > 512) {
                cerr << "ERROR: image too large to resize" << endl;
            }
            else {
                u8 *oldBuf = stbi_load_from_memory((const u8 *)data.c_str(), data.size(),
                    &origWidth, &origHeight, &origComp, 4);
                if (oldBuf == NULL) {
                    cerr << "stbi_load_from_memory() failed" << endl;
                }
                else {
                    memset(buf, 0, sizeof(buf));
                    int newWidth = (origWidth / 4 + 1) * 4;
                    int newHeight = (origHeight / 4 + 1) * 4;
                    uint32_t oldStride = origWidth * 4;
                    uint32_t newStride = newWidth * 4;
                    for (int y = origWidth - 1; y >= 0; y--) {
                        memcpy(buf + newStride * (origWidth - y - 1), oldBuf + oldStride * y, oldStride);
                    }
                    ostringstream newPngStream;
                    stbi_write_png_to_func(resized_sprite_write, (void *)&newPngStream,
                        newWidth, newHeight, 4, buf, newStride);
                    string newPng = newPngStream.str();
                    tex = GRRLIB_LoadTexturePNG((const u8 *)newPng.c_str());
                    stbi_image_free(oldBuf);
                    m_width = newWidth;
                    m_height = newHeight;
                }
            }
        }
        else {
            // load image directly
            tex = GRRLIB_LoadTexturePNG((u8 *)data.c_str());
        }
        if (tex == NULL) {
            cerr << "ERROR: load failed: " << path << endl;
            return;
        }
        m_texture = tex;
        m_valid = true;
    }
    virtual void draw(f32 xpos, f32 ypos, bool flipX) const {
        f32 scaleX;
        if (flipX) {
            scaleX = -1;
        }
        else {
            scaleX = 1;
        }
        GRRLIB_DrawImg(xpos, ypos, m_texture, 0, scaleX,
            1, 0xFFFFFFFF);

    }
    virtual ~MascotSpritePNG() {}
    bool valid() const {
        return m_valid;
    }
    virtual int width() const {
        return m_width;
    }
    virtual int height() const {
        return m_height;
    }
    GRRLIB_texImg *texture() const {
        return m_texture;
    }
private:
    bool m_valid;
    GRRLIB_texImg *m_texture;
    int m_width, m_height;
};

class TexturePack {
public:
    TexturePack() {}
    bool load(filesystem::path const& path) {
        if (m_sprites.size() != 0) {
            return false;
        }
        auto imgPath = path / "img";
        auto texPath = path / "textures";
        if (filesystem::is_directory(texPath)) {
            qutex::reader reader { texPath };
            GRRLIB_texImg *currentTexture = NULL;
            map<filesystem::path, GRRLIB_texImg *> textures;
            int cw, ch;
            reader.read_all_sprites(
                [&](std::filesystem::path path, int width, int height) {
                    if (m_sprites.count(path) == 0) {
                        currentTexture = textures[path] =
                            GRRLIB_LoadTextureFromFile(path.c_str());
                        cw = width;
                        ch = height;
                        if (currentTexture == NULL) {
                            cerr << "W: couldn't load: " << path << endl;
                            showConsoleNow();
                        }
                    }
                },
                [&](int x, int y, qutex::sprite_info const& info) {
                    auto sprite = new MascotSpriteQutex { currentTexture,
                        cw, ch, x, y, info.width, info.height, info.offset_x,
                        info.offset_y, info.real_width, info.real_height };
                    m_sprites[info.name] = sprite;
                }
            );
        }
        else if (filesystem::is_directory(imgPath)) {
            filesystem::directory_iterator imgIterator { imgPath };
            for (auto &entry : imgIterator) {
                auto path = entry.path();
                if (!entry.is_regular_file() || path.extension() != ".png") {
                    continue;
                }
                auto name = path.stem();
                auto png = new MascotSpritePNG { path };
                if (png->valid()) {
                    m_sprites[name] = png;
                }
                else {
                    delete png;
                }
            }
        }
        cout << "image count: " << m_sprites.size() << endl;
        for (auto &pair : m_sprites) {
            m_preview = pair.second;
            break;
        }
        return (m_sprites.size() > 0);
    }
    void clear() {
        for (auto tex : m_sprites) {
            delete tex.second;
        }
        m_sprites.clear();
    }
    MascotSprite *preview() {
        return m_preview;
    }
    const MascotSprite *sprite(string const& name) const {
        auto stem = (filesystem::path { name }).stem();
        if (m_sprites.count(stem)) {
            return m_sprites.at(stem);
        }
        else {
            return NULL;
        }
    }
    ~TexturePack() {
        clear();
    }
private:
    map<string, MascotSprite *> m_sprites;
    MascotSprite *m_preview;
};

class MascotData {
public:
    MascotData(): m_valid(false) {}
    bool valid() const {
        return m_valid;
    }
    string const& name() const {
        return m_name;
    }
    string const& behaviors() const {
        return m_behaviors;
    }
    string const& actions() const {
        return m_actions;
    }
    bool load(filesystem::path const& path) {
        auto actionsPath = path / "actions.xml";
        auto behaviorsPath = path / "behaviors.xml";
        m_name = path.stem();
        if (!filesystem::is_regular_file(actionsPath) ||
            !filesystem::is_regular_file(behaviorsPath))
        {
            return m_valid = false;
        }
        if (!readFile(actionsPath, m_actions) ||
            !readFile(behaviorsPath, m_behaviors))
        {
            return m_valid = false;
        }
        try {
            shijima::parser parser;
            parser.parse(m_actions, m_behaviors);
        }
        catch (std::exception &ex) {
            cerr << "ERROR: Parse failed for " << m_name << endl;
            cerr << "ERROR: " << ex.what() << endl;
            return m_valid = false;
        }
        m_graphics.clear();
        return m_graphics.load(path);
    }
    const MascotSprite *sprite(string const& name) const {
        return m_graphics.sprite(name);
    }
    const MascotSprite *preview() {
        return m_graphics.preview();
    }
private:
    bool m_valid;
    string m_name;
    string m_actions;
    string m_behaviors;
    TexturePack m_graphics;
};

static map<string, MascotData> loadedMascots;
static vector<MascotData *> loadedMascotsList;
static unique_ptr<shijima::mascot::factory> mascotFactory;
static shared_ptr<shijima::mascot::environment> mascotEnv;

class WiiMascot {
public:
    WiiMascot(): m_valid(false) {}
    WiiMascot(shijima::mascot::factory::product product, MascotData *data):
        m_valid(true), m_product(std::move(product)), m_data(data)
    {
        m_product.manager->reset_position();
    }
    bool valid() const {
        return m_valid;
    }
    void draw() {
        auto &mascot = *m_product.manager;
        auto pos = mascot.state->anchor;
        auto &frame = mascot.state->active_frame;
        bool mirroredRender = mascot.state->looking_right &&
            frame.right_name.empty();
        auto name = frame.get_name(mascot.state->looking_right);
        auto sprite = m_data->sprite(name);
        if (sprite == NULL) {
            return;
        }
        bool flip;
        if (mirroredRender) {
            flip = true;
            pos = { pos.x + frame.anchor.x,
                pos.y - frame.anchor.y };
        }
        else {
            flip = false;
            pos = { pos.x - frame.anchor.x, pos.y - frame.anchor.y };
        }
        m_lastPos = { pos.x, pos.y, (double)sprite->width(), (double)sprite->height() };
        if (mirroredRender) {
            m_lastPos.x -= sprite->width();
        }
        sprite->draw(pos.x, pos.y, flip);
        /*GRRLIB_Rectangle(m_lastPos.x, m_lastPos.y, m_lastPos.width,
            m_lastPos.height, 0x00FFFFFF, false);*/
    }
    void tick() {
        auto &mascot = *m_product.manager;
        mascot.tick();
    }
    shijima::mascot::manager &manager() {
        return *m_product.manager;
    }
    MascotData *data() const {
        return m_data;
    }
    bool pointInside(double x, double y) {
        return x >= m_lastPos.x && x < (m_lastPos.x + m_lastPos.width) &&
            y >= m_lastPos.y && y < (m_lastPos.y + m_lastPos.height);
    }
private:
    bool m_valid;
    shijima::mascot::factory::product m_product;
    MascotData *m_data;
    shijima::math::rec m_lastPos;
};

bool discoverMascots() {
    if (!filesystem::is_directory(MASCOT_LOCATION)) {
        die(MASCOT_LOCATION " missing!");
        return false;
    }
    filesystem::directory_iterator iter { MASCOT_LOCATION };
    for (auto &entry : iter) {
        if (!entry.is_directory()) {
            continue;
        }
        auto path = entry.path();
        if (path.extension() != ".mascot") {
            continue;
        }
        auto name = path.stem();
        cout << "Trying to load: " << name << endl;
        showConsoleNow();
        auto &mascot = loadedMascots[name];
        if (!mascot.load(path)) {
            loadedMascots.erase(name);
        }
    }
    mascotFactory = make_unique<shijima::mascot::factory>();
    for (auto &pair : loadedMascots) {
        shijima::mascot::factory::tmpl tmpl;
        auto &data = pair.second;
        tmpl.actions_xml = data.actions();
        tmpl.behaviors_xml = data.behaviors();
        tmpl.name = data.name();
        mascotFactory->register_template(tmpl);
        loadedMascotsList.push_back(&pair.second);
    }
    return loadedMascots.size() > 0;
}

void updateEnvironment() {
    auto &env = *mascotEnv;
    double width = rmode->fbWidth, height = rmode->efbHeight;
    env.work_area = { 0, width, height, 0 };
    env.screen = env.work_area;
    env.floor = { height, 0, width };
    env.ceiling = { 0, 0, width };
    env.active_ie = { -50, 50, -50, 50 };
}

static list<WiiMascot *> mascots;
static WiiMascot *dragged = nullptr;

WiiMascot *findMascot(double x, double y) {
    for (auto mascot : mascots) {
        if (mascot->pointInside(x, y)) {
            return mascot;
        }
    }
    return nullptr;
}

void shijimaWiiTick(struct ir_t const& ir, u32 down, u32 held, u32 up) {
    (void)up;
    static bool didStart = false;
    if (didStart) {
        static bool pickerVisible = false;
        static int pickerIdx = 0;
        bool irValid = ir.valid;
        if (pickerVisible) {
            irValid = false;
        }
        if (mascots.size() > 0) {
            bool isADown = ((held & WPAD_BUTTON_A) || (down & WPAD_BUTTON_A));
            updateEnvironment();
            if (irValid) {
                mascotEnv->cursor.move({ ir.x, ir.y });
                if (dragged == nullptr && isADown) {
                    auto target = findMascot(ir.x, ir.y);
                    if (target != nullptr) {
                        target->manager().state->dragging = true;
                        dragged = target;
                    }
                }
                if (down & WPAD_BUTTON_B) {
                    auto target = findMascot(ir.x, ir.y);
                    if (target != nullptr) {
                        target->manager().state->dead = true;
                    }
                }
            }
            if (dragged != nullptr && (!irValid || !isADown)) {
                dragged->manager().state->dragging = false;
                dragged = nullptr;
            }
            for (auto iter = mascots.end(); iter != mascots.begin(); ) {
                --iter;
                auto mascot = *iter;
                mascot->tick();
                if (mascot->manager().state->dead) {
                    auto erasePos = iter;
                    ++iter;
                    mascots.erase(erasePos);
                    delete mascot;
                    continue;
                }
                mascot->draw();
                auto &breedRequest = mascot->manager().state->breed_request;
                if (breedRequest.available) {
                    if (breedRequest.name == "") {
                        breedRequest.name = mascot->data()->name();
                    }
                    auto product = mascotFactory->spawn(breedRequest);
                    breedRequest.available = false;
                    mascots.push_back(new WiiMascot { std::move(product),
                        &loadedMascots.at(breedRequest.name) });
                }
            }
            mascotEnv->cursor.dx = mascotEnv->cursor.dy = 0;
        }
        if (down & WPAD_BUTTON_PLUS) {
            pickerVisible = !pickerVisible;
        }
        if (pickerVisible) {
            if ((down & WPAD_BUTTON_LEFT) && pickerIdx > 0) {
                --pickerIdx;
            }
            else if ((down & WPAD_BUTTON_RIGHT) && pickerIdx < (int)loadedMascotsList.size() - 1) {
                ++pickerIdx;
            }
            GRRLIB_Rectangle(0, 0, rmode->fbWidth, rmode->efbHeight, 0x00000088, true);
            auto data = loadedMascotsList[pickerIdx];
            auto preview = data->preview();
            preview->draw(rmode->fbWidth / 2 - preview->width() / 2,
                rmode->efbHeight / 2 - preview->height() / 2,
                false);
            if (pickerIdx != ((int)loadedMascotsList.size() - 1)) {
                GRRLIB_Printf(rmode->fbWidth / 2 + preview->width() / 2 + 8,
                    rmode->efbHeight / 2 - 8, texFont, 0xFFFFFFFF, 1,
                    "-->");
            }
            if (pickerIdx != 0) {
                GRRLIB_Printf(rmode->fbWidth / 2 - preview->width() / 2 - 32,
                    rmode->efbHeight / 2 - 8, texFont, 0xFFFFFFFF, 1,
                    "<--");
            }
            GRRLIB_Printf(rmode->fbWidth / 2 - data->name().size() * 4,
                rmode->efbHeight / 2 + preview->height() / 2 + 8, texFont,
                0xFFFFFFFF, 1, "%s", data->name().c_str());
            if (down & WPAD_BUTTON_A) {
                mascots.push_back(new WiiMascot { mascotFactory->spawn(data->name()),
                    data });
            }
            else if (down & WPAD_BUTTON_B) {
                for (auto iter = mascots.end(); iter != mascots.begin(); ) {
                    --iter;
                    auto mascot = *iter;
                    if (mascot->data() == data) {
                        auto erasePos = iter;
                        ++iter;
                        mascots.erase(erasePos);
                        delete mascot;
                        continue;
                    }
                }
            }
        }
        else if (irValid) {
            GRRLIB_Rectangle(ir.x - 1, ir.y - 1, 3, 3, 0xFF0000FF, true);
        }
    }
    else if (!didStart && (down & WPAD_BUTTON_A)) {
        clearConsole();
        didStart = true;
        cout << "Shijima-Wii. https://getshijima.app" << endl;
        cout << "Aim with Wiimote, hold [A] to drag, press [B] to dismiss" << endl;
        cout << "Press [+] to open shimeji picker, press [HOME] to exit" << endl;
        cout << endl;
    }
}

int main() {
    // Initialise the Graphics & Video subsystem
    GRRLIB_Init();

    // Initialise the Wiimotes
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);

    consoleLines.resize(rmode->efbHeight / 16 - 2);

    texFont = GRRLIB_LoadTexture(defaultFontTiles);
    GRRLIB_InitTileSet(texFont, defaultFontCharWidth,
        defaultFontCharHeight, defaultFontStart);

    try {
        if (!fatInitDefault()) {
            die("fatInitDefault failed!");
        }
        else if (!discoverMascots()) {
            die("Couldn't find any mascots!");
            //FIXME: not yet
            //cerr << "[*] You can use Shijima-Qt on a computer to prepare" << endl;
            //cerr << "shimeji for Shijima-Wii." << endl;
        }
        else {
            mascotEnv = make_shared<shijima::mascot::environment>();
            mascotEnv->subtick_count = 2;
            mascotFactory->env = mascotEnv;
            updateEnvironment();
            string mascotName;
            for (auto &pair : loadedMascots) {
                mascotName = pair.first;
                break;
            }
            mascots.push_back(new WiiMascot { mascotFactory->spawn(mascotName),
                &loadedMascots.at(mascotName) });
            cout << "... Press A to start Shijima-Wii" << endl;
        }
    }
    catch (std::exception &ex) {
        die(ex.what());
    }

    while (1) {
        WPAD_ScanPads();
        struct ir_t ir;
        WPAD_IR(WPAD_CHAN_0, &ir);
        u32 down = WPAD_ButtonsDown(WPAD_CHAN_0);
        u32 held = WPAD_ButtonsHeld(WPAD_CHAN_0);
        u32 up = WPAD_ButtonsUp(WPAD_CHAN_0);

        if (down & WPAD_BUTTON_HOME) break;

        // console
        flushConsole();
        drawConsole();
        
        // tick and draw graphics if not crashed
        if (!fatalError) {
            try {
                shijimaWiiTick(ir, down, held, up);
            }
            catch (std::exception &ex) {
                die(ex.what());
            }
        }

        GRRLIB_Render();
    }

    GRRLIB_Exit();

    exit(0); // required according to GRRLIB examples
}
