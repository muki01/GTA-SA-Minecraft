#include "GameState.h"

#include "Audio.h"
#include "Entities.h"
#include "Host.h"
#include "Inventory.h"
#include "Items.h"

namespace mc {

GameState gGame;
GameRules gRules;

void GameTimersTick(float dt) {
    gGame.messageTimer = std::max(0.0f, gGame.messageTimer - dt);
    gGame.selectedNameTimer = std::max(0.0f, gGame.selectedNameTimer - dt);
    gGame.attackTimer += dt;
}

void ShowMessage(const std::string& text, float seconds) {
    gGame.message = text;
    gGame.messageTimer = seconds;
}

void StartSwing() {
    if (gGame.offhandActive) {
        if (gGame.offSwing < 0.0f || gGame.offSwing > 0.5f)
            gGame.offSwing = 0.0f;
        return;
    }
    if (gGame.swing < 0.0f || gGame.swing > 0.5f)
        gGame.swing = 0.0f;
}

float AttackCharge() {
    const ItemStack& h = gInv.Held();
    float speed = h.Empty() ? 4.0f : Item(h.id).attackSpeed;
    if (speed <= 0.0f)
        speed = 4.0f;
    return Clamp(gGame.attackTimer * speed, 0.0f, 1.0f);
}

void DamageHeldItem(int amount) {
    ItemStack& h = gInv.Held();
    if (h.Empty() || gGame.gameMode == MODE_CREATIVE)
        return;
    const ItemDef& d = Item(h.id);
    if (!d.durability)
        return;
    h.damage += (uint16_t)amount;
    if (h.damage >= d.durability) {
        ShowMessage(std::string(d.name) + " k\xC4\xB1r\xC4\xB1ld\xC4\xB1!", 1.5f);
        PlaySfx(SND_TOOL_BREAK);
        h.Clear();
    }
}

// ---------------------------------------------------------------- screens
void OpenScreen(int screen, const Int3& pos) {
    gGame.screen = screen;
    gGame.openPos = pos;
    gGame.cursorX = gGame.viewW * 0.5f;
    gGame.cursorY = gGame.viewH * 0.5f;
    if (screen == SCREEN_FURNACE && !gWorld.furnaces.count(pos))
        gWorld.furnaces[pos] = FurnaceState();
    if (screen == SCREEN_CHEST) {
        if (!gWorld.chests.count(pos))
            gWorld.chests[pos] = ChestState();
        Vec3 c(pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f);
        PlaySfx(SND_CHEST_OPEN, &c);
    }
}

void DropStackAtPlayer(const ItemStack& s, bool thrown) {
    Vec3 p;
    if (!TheHost().PlayerPos(&p) || s.Empty())
        return;
    Vec3 dir = gGame.lookDir;
    Vec3 pos(p.x + dir.x * 0.6f, p.y + dir.y * 0.6f, p.z + 0.4f);
    Vec3 vel = thrown ? Vec3(dir.x * 4.0f, dir.y * 4.0f, 2.5f) : Vec3(0, 0, 1.0f);
    SpawnDrop(pos, s, vel, 1.5f);
}

void CloseScreen() {
    auto give = [](ItemStack& s) {
        if (s.Empty())
            return;
        int left = gInv.Add(s);
        if (left > 0) {
            ItemStack rest = s;
            rest.count = (uint8_t)left;
            DropStackAtPlayer(rest, false);
        }
        s.Clear();
    };
    for (auto& s : gInv.craft)
        give(s);
    for (auto& s : gInv.craft3)
        give(s);
    give(gInv.cursor);
    if (gGame.screen == SCREEN_CHEST) {
        Vec3 c(gGame.openPos.x + 0.5f, gGame.openPos.y + 0.5f, gGame.openPos.z + 0.5f);
        PlaySfx(SND_CHEST_CLOSE, &c);
    }
    gGame.screen = SCREEN_NONE;
    gWorld.dirty = true;
}

void PlayerDied(const Vec3& at) {
    if (gGame.screen != SCREEN_NONE)
        CloseScreen();
    PlaySfx(SND_PLAYER_DEATH);
    if (!gRules.keepInventory && gGame.gameMode == MODE_SURVIVAL) {
        for (auto& s : gInv.slots) {
            SpawnDrop(at, s, Vec3(0, 0, 0), 2.0f);
            s.Clear();
        }
        for (auto& s : gInv.armor) {
            SpawnDrop(at, s, Vec3(0, 0, 0), 2.0f);
            s.Clear();
        }
    }
}

void GiveStarterKit() {
    if (!gWorld.chunks.empty() || gInv.CountOf(ID_CRAFTING_TABLE) != 0 || !gInv.slots[0].Empty())
        return;
    ItemStack s;
    s.id = ID_CRAFTING_TABLE; s.count = 1; gInv.Add(s);
    s.id = ID_OAK_PLANKS; s.count = 16; gInv.Add(s);
    s.id = ID_APPLE; s.count = 4; gInv.Add(s);
}

} // namespace mc
