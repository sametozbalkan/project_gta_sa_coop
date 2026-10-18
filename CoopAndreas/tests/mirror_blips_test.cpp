// Private build: policy test for the mission mirror layer's blips without GTA (client/src/CMirrorBlipsPolicy.h, used
// by CMirrorBlips): ownership (the creating commands, blip handles, the owned set), the host's per-frame difference
// (full state, position updates and their rates), the receiver's local blip (entity, follow, coordinate, none on its
// own player), the switch from a followed position to the entity blip, the mirrored table and the sender check. Build
// and run: see CLAUDE.md (Fork changes, mission mirror layer, blips).
#include "../client/src/CMirrorBlipsPolicy.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace MirrorBlipsPolicy;

namespace
{
uint32_t Handle(uint32_t index, uint32_t counter)
{
    return index | (counter << 16);
}

SBlipState CoordBlip(float x, float y, float z)
{
    SBlipState state;
    state.type = Type::COORD;
    state.x = x;
    state.y = y;
    state.z = z;
    state.colour = 8;  // SetCoordBlip's default (BLIP_COLOUR_DESTINATION)
    state.display = Display::BOTH;
    state.size = 3;    // ChangeBlipScale(3) in the 018A case
    return state;
}

SBlipState EntityBlip(uint8_t type, eEntityKind kind, int id, float x, float y, float z)
{
    SBlipState state;
    state.type = type;
    state.entityKind = kind;
    state.entityId = id;
    state.x = x;
    state.y = y;
    state.z = z;
    state.colour = 7;  // SetEntityBlip's default for cars and chars (BLIP_COLOUR_THREAT)
    state.display = Display::BOTH;
    state.size = 3;
    return state;
}
}  // namespace

void CreatingCommands()
{
    const uint16_t creating[] = {0x0161, 0x0167, 0x0186, 0x0187, 0x0188, 0x018A, 0x02A7, 0x02A8, 0x03DC, 0x04CE, 0x0570,
        0x06C4, 0x0888};
    for (uint16_t opcode : creating)
    {
        CHECK(IsCreatingCommand(opcode));
    }
    // the change and removal commands are covered by the trace state, not by ownership; race checkpoints and script
    // spheres (06D5, 03BC) belong to package M5
    const uint16_t others[] = {0x0164, 0x0165, 0x0168, 0x018B, 0x07BF, 0x07E0, 0x08DC, 0x08FB, 0x06D5, 0x03BC, 0x00BC,
        0x0000};
    for (uint16_t opcode : others)
    {
        CHECK(!IsCreatingCommand(opcode));
    }
}

void BlipHandles()
{
    CHECK(!IsBlipHandle(0xFFFFFFFFu));          // no free trace
    CHECK(!IsBlipHandle(0));                    // counter 0: never handed out
    CHECK(IsBlipHandle(Handle(0, 1)));
    CHECK(IsBlipHandle(Handle(174, 0xFFFE)));
    CHECK(!IsBlipHandle(Handle(175, 1)));        // past the last trace
    CHECK(!IsBlipHandle(Handle(0xFFFF, 0xFFFF)));
}

void OwnedSet()
{
    COwnedBlips owned;
    CHECK(owned.Count() == 0);
    CHECK(owned.Add(Handle(3, 7)));
    CHECK(!owned.Add(Handle(3, 7)));             // owned already
    CHECK(owned.Add(Handle(3, 8)));              // the same trace used again: another blip
    CHECK(!owned.Add(0xFFFFFFFFu));
    CHECK(owned.Count() == 2);
    CHECK(owned.Contains(Handle(3, 7)) && owned.Contains(Handle(3, 8)) && !owned.Contains(Handle(4, 7)));

    // a new owned blip has nothing sent
    CHECK(!owned.At(0).bSent && owned.At(0).positionSentAt == 0);

    owned.RemoveAt(0);
    CHECK(owned.Count() == 1 && owned.At(0).handle == Handle(3, 8) && !owned.Contains(Handle(3, 7)));
    owned.RemoveAt(5);                           // out of range: nothing
    CHECK(owned.Count() == 1);

    owned.Clear();
    for (uint32_t i = 0; i < static_cast<uint32_t>(MAX_BLIPS); i++)
    {
        CHECK(owned.Add(Handle(i, 1)));
    }
    CHECK(owned.Count() == MAX_BLIPS);
    CHECK(!owned.Add(Handle(0, 2)));             // full
    owned.Clear();
    CHECK(owned.Count() == 0 && owned.Add(Handle(0, 2)));
}

void Difference()
{
    // never sent: the whole state
    const SBlipState coord = CoordBlip(2495.0f, -1665.0f, 13.3f);
    CHECK(DecideSend(false, SBlipState{}, coord, 0) == eSend::FULL);
    // unchanged: nothing
    CHECK(DecideSend(true, coord, coord, 100000) == eSend::NONE);

    // every look change goes out in full (colour, scale, display, sprite, friendly, short range, remain, fade,
    // appearance, brightness)
    SBlipState changed = coord;
    changed.colour = 2;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.colour = 0xFF00FFFFu;  // a raw RGBA colour
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.size = 2;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.display = Display::BLIP_ONLY;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.sprite = 38;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.bFriendly = true;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.bShortRange = true;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.bRemain = true;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.bFade = true;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.coordAppearance = 2;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.bBright = false;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);

    // an entrance (set_blip_entry_exit), and another entrance
    changed = coord;
    changed.bEntrance = true;
    changed.entranceX = 2495.4f;
    changed.entranceY = -1691.1f;
    changed.entranceZ = 14.8f;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    SBlipState other = changed;
    other.entranceY = -1600.0f;
    CHECK(DecideSend(true, changed, other, 0) == eSend::FULL);
    CHECK(DecideSend(true, changed, changed, 0) == eSend::NONE);
    // entrance coordinates without the flag don't count
    SBlipState noFlag = coord;
    noFlag.entranceX = 123.0f;
    CHECK(DecideSend(true, coord, noFlag, 0) == eSend::NONE);

    // a coordinate blip doesn't move: a real change goes out in full, float noise doesn't
    changed = coord;
    changed.x += 1.0f;
    CHECK(DecideSend(true, coord, changed, 0) == eSend::FULL);
    changed = coord;
    changed.x += 0.005f;
    CHECK(DecideSend(true, coord, changed, 100000) == eSend::NONE);

    // a blip the others show at the host's position (an object): at most every 250 ms, from 0.5 m on
    const SBlipState object = EntityBlip(Type::OBJECT, eEntityKind::NONE, 0, 100.0f, 100.0f, 10.0f);
    SBlipState moved = object;
    moved.x += 1.0f;
    CHECK(DecideSend(true, object, moved, FOLLOW_INTERVAL_MS) == eSend::POSITION);
    CHECK(DecideSend(true, object, moved, FOLLOW_INTERVAL_MS - 1) == eSend::NONE);
    moved = object;
    moved.y += 0.3f;
    CHECK(DecideSend(true, object, moved, 10000) == eSend::NONE);
    // a ped without a network id yet follows the same way
    const SBlipState unconfirmed = EntityBlip(Type::CHAR, eEntityKind::NONE, 0, 0.0f, 0.0f, 0.0f);
    moved = unconfirmed;
    moved.z += 0.6f;
    CHECK(DecideSend(true, unconfirmed, moved, 300) == eSend::POSITION);

    // an entity blip on a network entity: 0.5 Hz from 2 m on
    const SBlipState car = EntityBlip(Type::CAR, eEntityKind::VEHICLE, 12, 0.0f, 0.0f, 0.0f);
    moved = car;
    moved.x += 3.0f;
    CHECK(DecideSend(true, car, moved, ENTITY_POSITION_INTERVAL_MS - 1) == eSend::NONE);
    CHECK(DecideSend(true, car, moved, ENTITY_POSITION_INTERVAL_MS) == eSend::POSITION);
    moved = car;
    moved.x += 1.5f;
    CHECK(DecideSend(true, car, moved, 100000) == eSend::NONE);

    // an owned object blip (eEntityKind::OBJECT): uses entity interval (0.5 Hz / 2 m) instead of 4 Hz follow
    const SBlipState ownedObj = EntityBlip(Type::OBJECT, eEntityKind::OBJECT, 0x0502, 100.0f, 100.0f, 10.0f);
    moved = ownedObj;
    moved.x += 1.0f;  // >= 0.5 m follow threshold, but < 2.0 m entity threshold
    CHECK(DecideSend(true, ownedObj, moved, FOLLOW_INTERVAL_MS) == eSend::NONE);
    CHECK(DecideSend(true, ownedObj, moved, ENTITY_POSITION_INTERVAL_MS) == eSend::NONE);
    moved = ownedObj;
    moved.x += 2.5f;  // >= 2.0 m entity threshold
    CHECK(DecideSend(true, ownedObj, moved, FOLLOW_INTERVAL_MS) == eSend::NONE);
    CHECK(DecideSend(true, ownedObj, moved, ENTITY_POSITION_INTERVAL_MS - 1) == eSend::NONE);
    CHECK(DecideSend(true, ownedObj, moved, ENTITY_POSITION_INTERVAL_MS) == eSend::POSITION);
    // switching from unowned object (NONE) to owned object (OBJECT), or changing object handle -> FULL
    CHECK(DecideSend(true, object, ownedObj, 0) == eSend::FULL);
    SBlipState otherObj = ownedObj;
    otherObj.entityId = 0x0503;
    CHECK(DecideSend(true, ownedObj, otherObj, 0) == eSend::FULL);

    // the confirm gives the vehicle its network id: the whole state, so the others switch to the entity blip
    SBlipState pending = car;
    pending.entityKind = eEntityKind::NONE;
    pending.entityId = 0;
    CHECK(DecideSend(true, pending, car, 0) == eSend::FULL);
    // another entity, or another kind
    SBlipState otherCar = car;
    otherCar.entityId = 13;
    CHECK(DecideSend(true, car, otherCar, 0) == eSend::FULL);
    SBlipState ped = car;
    ped.entityKind = eEntityKind::PED;
    CHECK(DecideSend(true, car, ped, 0) == eSend::FULL);
    // without a kind the id doesn't matter
    SBlipState noKindA = pending;
    SBlipState noKindB = pending;
    noKindA.entityId = 0;
    noKindB.entityId = 99;
    CHECK(DecideSend(true, noKindA, noKindB, 0) == eSend::NONE);
    // a type change (should not happen for a handle, but it is a new look)
    SBlipState asChar = car;
    asChar.type = Type::CHAR;
    CHECK(DecideSend(true, car, asChar, 0) == eSend::FULL);
}

void LocalForms()
{
    // coordinate and contact point blips are made as the host has them
    const SBlipState coord = CoordBlip(1.0f, 2.0f, 3.0f);
    CHECK(DecideLocalForm(coord, false, false) == eLocalForm::COORD);
    CHECK(LocalType(coord, eLocalForm::COORD) == Type::COORD);
    SBlipState contact = coord;
    contact.type = Type::CONTACT_POINT;
    CHECK(DecideLocalForm(contact, false, false) == eLocalForm::COORD);
    CHECK(LocalType(contact, eLocalForm::COORD) == Type::CONTACT_POINT);
    SBlipState airstrip = coord;
    airstrip.type = Type::AIRSTRIP;
    CHECK(DecideLocalForm(airstrip, false, false) == eLocalForm::COORD);
    CHECK(LocalType(airstrip, eLocalForm::COORD) == Type::COORD);
    CHECK(LocalDisplay(coord, eLocalForm::COORD) == Display::BOTH);

    // a car with a network id: the entity blip once this game has the car, else following the host's position
    const SBlipState car = EntityBlip(Type::CAR, eEntityKind::VEHICLE, 4, 0.0f, 0.0f, 0.0f);
    CHECK(DecideLocalForm(car, true, false) == eLocalForm::ENTITY);
    CHECK(LocalType(car, eLocalForm::ENTITY) == Type::CAR);
    CHECK(DecideLocalForm(car, false, false) == eLocalForm::FOLLOW);
    CHECK(LocalType(car, eLocalForm::FOLLOW) == Type::COORD);

    // an owned object (eEntityKind::OBJECT): FOLLOW before the copy resolves, ENTITY with Type::OBJECT once resolved
    const SBlipState obj = EntityBlip(Type::OBJECT, eEntityKind::OBJECT, 0x0502, 10.0f, 20.0f, 5.0f);
    CHECK(DecideLocalForm(obj, false, false) == eLocalForm::FOLLOW);
    CHECK(LocalType(obj, eLocalForm::FOLLOW) == Type::COORD);
    CHECK(DecideLocalForm(obj, true, false) == eLocalForm::ENTITY);
    CHECK(LocalType(obj, eLocalForm::ENTITY) == Type::OBJECT);
    CHECK(DecideLocalForm(obj, false, true) == eLocalForm::FOLLOW);
    CHECK(DecideLocalForm(obj, true, true) == eLocalForm::ENTITY);

    // peds and players get char blips
    const SBlipState ped = EntityBlip(Type::CHAR, eEntityKind::PED, 0, 0.0f, 0.0f, 0.0f);  // id 0 is a valid id
    CHECK(DecideLocalForm(ped, true, false) == eLocalForm::ENTITY);
    CHECK(LocalType(ped, eLocalForm::ENTITY) == Type::CHAR);
    const SBlipState player = EntityBlip(Type::CHAR, eEntityKind::PLAYER, 1, 0.0f, 0.0f, 0.0f);
    CHECK(DecideLocalForm(player, true, false) == eLocalForm::ENTITY);
    CHECK(LocalType(player, eLocalForm::ENTITY) == Type::CHAR);
    // no blip on this game's own player, whether its ped is found or not
    CHECK(DecideLocalForm(player, false, true) == eLocalForm::NONE);
    CHECK(DecideLocalForm(player, true, true) == eLocalForm::NONE);
    CHECK(LocalType(player, eLocalForm::NONE) == Type::NONE);
    // the local player flag means nothing for a ped or a car
    CHECK(DecideLocalForm(ped, false, true) == eLocalForm::FOLLOW);

    // objects, pickups, searchlights, trains and unconfirmed entities always follow
    const uint8_t followTypes[] = {Type::OBJECT, Type::PICKUP, Type::SPOTLIGHT, Type::CAR, Type::CHAR};
    for (uint8_t type : followTypes)
    {
        const SBlipState blip = EntityBlip(type, eEntityKind::NONE, 0, 0.0f, 0.0f, 0.0f);
        CHECK(DecideLocalForm(blip, true, false) == eLocalForm::FOLLOW);
        CHECK(DecideLocalForm(blip, false, false) == eLocalForm::FOLLOW);
    }

    // following: radar only, no ground marker
    SBlipState display = car;
    display.display = Display::BOTH;
    CHECK(LocalDisplay(display, eLocalForm::FOLLOW) == Display::BLIP_ONLY);
    display.display = Display::BLIP_ONLY;
    CHECK(LocalDisplay(display, eLocalForm::FOLLOW) == Display::BLIP_ONLY);
    display.display = Display::MARKER_ONLY;
    CHECK(LocalDisplay(display, eLocalForm::FOLLOW) == Display::NEITHER);
    display.display = Display::NEITHER;
    CHECK(LocalDisplay(display, eLocalForm::FOLLOW) == Display::NEITHER);
    // the entity blip keeps the host's display (its marker cone sits on this game's copy)
    display.display = Display::MARKER_ONLY;
    CHECK(LocalDisplay(display, eLocalForm::ENTITY) == Display::MARKER_ONLY);
}

void Switching()
{
    // no local blip yet, or its trace is gone (the game cleared it with its entity)
    CHECK(NeedsRebuild(false, eLocalForm::NONE, Type::NONE, 0, eLocalForm::FOLLOW, Type::COORD, 0));
    CHECK(NeedsRebuild(false, eLocalForm::ENTITY, Type::CAR, 0x1201, eLocalForm::ENTITY, Type::CAR, 0x1201));
    // in line: only the fields are written
    CHECK(!NeedsRebuild(true, eLocalForm::FOLLOW, Type::COORD, 0, eLocalForm::FOLLOW, Type::COORD, 0));
    CHECK(!NeedsRebuild(true, eLocalForm::ENTITY, Type::CAR, 0x1201, eLocalForm::ENTITY, Type::CAR, 0x1201));
    CHECK(!NeedsRebuild(true, eLocalForm::COORD, Type::CONTACT_POINT, 0, eLocalForm::COORD, Type::CONTACT_POINT, 0));
    // the pool reference only matters for an entity blip
    CHECK(!NeedsRebuild(true, eLocalForm::FOLLOW, Type::COORD, 5, eLocalForm::FOLLOW, Type::COORD, 0));

    // A car blip that arrives before the car: following; then the car is there: the entity blip; then this game's copy
    // is recreated in another pool slot: a new entity blip; then it is gone: following again.
    const SBlipState car = EntityBlip(Type::CAR, eEntityKind::VEHICLE, 9, 10.0f, 20.0f, 5.0f);
    eLocalForm form = DecideLocalForm(car, false, false);
    uint8_t type = LocalType(car, form);
    CHECK(form == eLocalForm::FOLLOW && type == Type::COORD);
    CHECK(NeedsRebuild(false, eLocalForm::NONE, Type::NONE, 0, form, type, 0));
    eLocalForm haveForm = form;
    uint8_t haveType = type;
    int haveRef = 0;

    form = DecideLocalForm(car, true, false);
    type = LocalType(car, form);
    CHECK(form == eLocalForm::ENTITY && type == Type::CAR);
    CHECK(NeedsRebuild(true, haveForm, haveType, haveRef, form, type, 0x0A07));
    haveForm = form;
    haveType = type;
    haveRef = 0x0A07;
    CHECK(!NeedsRebuild(true, haveForm, haveType, haveRef, form, type, 0x0A07));
    CHECK(NeedsRebuild(true, haveForm, haveType, haveRef, form, type, 0x1508));

    form = DecideLocalForm(car, false, false);
    type = LocalType(car, form);
    CHECK(form == eLocalForm::FOLLOW);
    CHECK(NeedsRebuild(true, haveForm, haveType, haveRef, form, type, 0));

    // An object blip on an owned object: FOLLOW while the copy is unresolved, then switches to ENTITY (Type::OBJECT),
    // rebuilds if the local copy's pool handle changes, and switches back to FOLLOW if the copy is lost.
    const SBlipState obj = EntityBlip(Type::OBJECT, eEntityKind::OBJECT, 0x15D7F, 15.0f, 25.0f, 3.0f);
    form = DecideLocalForm(obj, false, false);
    type = LocalType(obj, form);
    CHECK(form == eLocalForm::FOLLOW && type == Type::COORD);
    CHECK(NeedsRebuild(false, eLocalForm::NONE, Type::NONE, 0, form, type, 0));
    haveForm = form;
    haveType = type;
    haveRef = 0;

    form = DecideLocalForm(obj, true, false);
    type = LocalType(obj, form);
    CHECK(form == eLocalForm::ENTITY && type == Type::OBJECT);
    CHECK(NeedsRebuild(true, haveForm, haveType, haveRef, form, type, 0x0301));
    haveForm = form;
    haveType = type;
    haveRef = 0x0301;
    CHECK(!NeedsRebuild(true, haveForm, haveType, haveRef, form, type, 0x0301));
    CHECK(NeedsRebuild(true, haveForm, haveType, haveRef, form, type, 0x0302));  // new object ref in same or other slot

    form = DecideLocalForm(obj, false, false);
    type = LocalType(obj, form);
    CHECK(form == eLocalForm::FOLLOW && type == Type::COORD);
    CHECK(NeedsRebuild(true, haveForm, haveType, haveRef, form, type, 0));

    // the host's coordinate blip becomes a contact point (should not happen for a handle): a new local blip
    SBlipState coord = CoordBlip(0.0f, 0.0f, 0.0f);
    SBlipState contact = coord;
    contact.type = Type::CONTACT_POINT;
    CHECK(NeedsRebuild(true, eLocalForm::COORD, LocalType(coord, eLocalForm::COORD), 0, eLocalForm::COORD,
        LocalType(contact, eLocalForm::COORD), 0));
}

void MirroredTable()
{
    CMirroredBlips table;
    CHECK(table.Count() == 0 && table.Find(Handle(1, 1)) == nullptr);

    // a full state makes an entry without a local blip
    SMirroredBlip* pBlip = table.Upsert(Handle(1, 1), CoordBlip(1.0f, 2.0f, 3.0f));
    CHECK(pBlip != nullptr && table.Count() == 1);
    CHECK(pBlip->localHandle == -1 && pBlip->form == eLocalForm::NONE && pBlip->host.x == 1.0f);
    pBlip->localHandle = static_cast<int>(Handle(40, 3));
    pBlip->form = eLocalForm::COORD;

    // another full state of the same blip keeps the local blip
    pBlip = table.Upsert(Handle(1, 1), CoordBlip(5.0f, 2.0f, 3.0f));
    CHECK(table.Count() == 1 && pBlip->host.x == 5.0f && pBlip->localHandle == static_cast<int>(Handle(40, 3)));
    CHECK(pBlip->form == eLocalForm::COORD);

    // the same trace used again by the host is another blip
    CHECK(table.Upsert(Handle(1, 2), CoordBlip(0.0f, 0.0f, 0.0f)) != nullptr && table.Count() == 2);

    // removal gives the local blip back for clearing
    int localHandle = 0;
    CHECK(table.Remove(Handle(1, 1), localHandle) && localHandle == static_cast<int>(Handle(40, 3)));
    CHECK(table.Count() == 1 && table.Find(Handle(1, 1)) == nullptr && table.Find(Handle(1, 2)) != nullptr);
    CHECK(!table.Remove(Handle(1, 1), localHandle) && localHandle == -1);
    CHECK(table.Remove(Handle(1, 2), localHandle) && localHandle == -1);
    CHECK(table.Count() == 0);

    // full
    for (uint32_t i = 0; i < static_cast<uint32_t>(MAX_BLIPS); i++)
    {
        CHECK(table.Upsert(Handle(i, 1), SBlipState{}) != nullptr);
    }
    CHECK(table.Upsert(Handle(0, 2), SBlipState{}) == nullptr);
    CHECK(table.Upsert(Handle(0, 1), CoordBlip(1.0f, 1.0f, 1.0f)) != nullptr);  // an update still works when full
    table.Clear();
    CHECK(table.Count() == 0);
}

void SenderCheck()
{
    CHECK(AcceptFrom(false, true, 2, 2));
    CHECK(AcceptFrom(false, true, 0, 0));
    CHECK(!AcceptFrom(false, true, 2, 1));   // a former host's packet still in the buffer
    CHECK(!AcceptFrom(false, false, -1, 2)); // nobody is the host (it left)
    CHECK(!AcceptFrom(true, true, 1, 1));    // this game is the host
}

void Types()
{
    CHECK(!IsValidType(Type::NONE) && IsValidType(Type::CAR) && IsValidType(Type::AIRSTRIP) && !IsValidType(9));
    CHECK(IsCoordinateType(Type::COORD) && IsCoordinateType(Type::CONTACT_POINT) && IsCoordinateType(Type::AIRSTRIP));
    CHECK(!IsCoordinateType(Type::CAR) && !IsCoordinateType(Type::CHAR) && !IsCoordinateType(Type::OBJECT) &&
          !IsCoordinateType(Type::SPOTLIGHT) && !IsCoordinateType(Type::PICKUP));
}

int main()
{
    CreatingCommands();
    BlipHandles();
    OwnedSet();
    Difference();
    LocalForms();
    Switching();
    MirroredTable();
    SenderCheck();
    Types();
    std::printf("mirror_blips_test: all checks passed\n");
    return 0;
}
