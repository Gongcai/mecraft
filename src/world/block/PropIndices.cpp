#include "PropIndices.h"

#include "BlockStateRegistry.h"

namespace PropIndices {

uint16_t FACING = INVALID;
uint16_t AXIS = INVALID;
uint16_t HALF = INVALID;
uint16_t OPEN = INVALID;
uint16_t POWERED = INVALID;
uint16_t LIT = INVALID;
uint16_t WATERLOGGED = INVALID;
uint16_t LEVEL = INVALID;
uint16_t FALLING = INVALID;
uint16_t SHAPE = INVALID;
uint16_t PART = INVALID;
uint16_t NORTH = INVALID;
uint16_t SOUTH = INVALID;
uint16_t EAST = INVALID;
uint16_t WEST = INVALID;
uint16_t EXTENDED = INVALID;
uint16_t POWER = INVALID;
uint16_t TYPE = INVALID;
uint16_t DELAY = INVALID;
uint16_t MODE = INVALID;

uint16_t FACING_FLOOR = INVALID;
uint16_t FACING_NORTH = INVALID;
uint16_t FACING_SOUTH = INVALID;
uint16_t FACING_EAST = INVALID;
uint16_t FACING_WEST = INVALID;
uint16_t FACING_UP = INVALID;
uint16_t FACING_DOWN = INVALID;

uint16_t AXIS_X = INVALID;
uint16_t AXIS_Y = INVALID;
uint16_t AXIS_Z = INVALID;

uint16_t HALF_TOP = INVALID;
uint16_t HALF_BOTTOM = INVALID;
uint16_t HALF_DOUBLE = INVALID;
uint16_t HALF_NORTH = INVALID;
uint16_t HALF_SOUTH = INVALID;
uint16_t HALF_EAST = INVALID;
uint16_t HALF_WEST = INVALID;

uint16_t OPEN_TRUE = INVALID;
uint16_t OPEN_FALSE = INVALID;
uint16_t POWERED_TRUE = INVALID;
uint16_t POWERED_FALSE = INVALID;
uint16_t LIT_TRUE = INVALID;
uint16_t LIT_FALSE = INVALID;
uint16_t WATERLOGGED_TRUE = INVALID;
uint16_t WATERLOGGED_FALSE = INVALID;
uint16_t LEVEL_0 = INVALID;
uint16_t LEVEL_1 = INVALID;
uint16_t LEVEL_2 = INVALID;
uint16_t LEVEL_3 = INVALID;
uint16_t LEVEL_4 = INVALID;
uint16_t LEVEL_5 = INVALID;
uint16_t LEVEL_6 = INVALID;
uint16_t LEVEL_7 = INVALID;
uint16_t FALLING_TRUE = INVALID;
uint16_t FALLING_FALSE = INVALID;
uint16_t SHAPE_STRAIGHT = INVALID;
uint16_t SHAPE_INNER_LEFT = INVALID;
uint16_t SHAPE_INNER_RIGHT = INVALID;
uint16_t SHAPE_OUTER_LEFT = INVALID;
uint16_t SHAPE_OUTER_RIGHT = INVALID;
uint16_t PART_HEAD = INVALID;
uint16_t PART_FOOT = INVALID;
uint16_t NORTH_TRUE = INVALID;
uint16_t NORTH_FALSE = INVALID;
uint16_t SOUTH_TRUE = INVALID;
uint16_t SOUTH_FALSE = INVALID;
uint16_t EAST_TRUE = INVALID;
uint16_t EAST_FALSE = INVALID;
uint16_t WEST_TRUE = INVALID;
uint16_t WEST_FALSE = INVALID;
uint16_t EXTENDED_TRUE = INVALID;
uint16_t EXTENDED_FALSE = INVALID;
uint16_t POWER_0 = INVALID;
uint16_t POWER_1 = INVALID;
uint16_t POWER_2 = INVALID;
uint16_t POWER_3 = INVALID;
uint16_t POWER_4 = INVALID;
uint16_t POWER_5 = INVALID;
uint16_t POWER_6 = INVALID;
uint16_t POWER_7 = INVALID;
uint16_t POWER_8 = INVALID;
uint16_t POWER_9 = INVALID;
uint16_t POWER_10 = INVALID;
uint16_t POWER_11 = INVALID;
uint16_t POWER_12 = INVALID;
uint16_t POWER_13 = INVALID;
uint16_t POWER_14 = INVALID;
uint16_t POWER_15 = INVALID;
uint16_t TYPE_NORMAL = INVALID;
uint16_t TYPE_STICKY = INVALID;
uint16_t DELAY_1 = INVALID;
uint16_t DELAY_2 = INVALID;
uint16_t DELAY_3 = INVALID;
uint16_t DELAY_4 = INVALID;
uint16_t MODE_COMPARE = INVALID;
uint16_t MODE_SUBTRACT = INVALID;

namespace {
uint16_t lookupName(const char* name) {
    return BlockStateRegistry::getPropertyNameIndex(name);
}

uint16_t lookupValue(const uint16_t nameIndex, const char* value) {
    return BlockStateRegistry::getPropertyValueIndex(nameIndex, value);
}
}

void init() {
    FACING = lookupName("facing");
    AXIS = lookupName("axis");
    HALF = lookupName("half");
    OPEN = lookupName("open");
    POWERED = lookupName("powered");
    LIT = lookupName("lit");
    WATERLOGGED = lookupName("waterlogged");
    LEVEL = lookupName("level");
    FALLING = lookupName("falling");
    SHAPE = lookupName("shape");
    PART = lookupName("part");
    NORTH = lookupName("north");
    SOUTH = lookupName("south");
    EAST = lookupName("east");
    WEST = lookupName("west");
    EXTENDED = lookupName("extended");
    POWER = lookupName("power");
    TYPE = lookupName("type");
    DELAY = lookupName("delay");
    MODE = lookupName("mode");

    FACING_FLOOR = lookupValue(FACING, "floor");
    FACING_NORTH = lookupValue(FACING, "north");
    FACING_SOUTH = lookupValue(FACING, "south");
    FACING_EAST = lookupValue(FACING, "east");
    FACING_WEST = lookupValue(FACING, "west");
    FACING_UP = lookupValue(FACING, "up");
    FACING_DOWN = lookupValue(FACING, "down");

    AXIS_X = lookupValue(AXIS, "x");
    AXIS_Y = lookupValue(AXIS, "y");
    AXIS_Z = lookupValue(AXIS, "z");

    HALF_TOP = lookupValue(HALF, "top");
    HALF_BOTTOM = lookupValue(HALF, "bottom");
    HALF_DOUBLE = lookupValue(HALF, "double");
    HALF_NORTH = lookupValue(HALF, "north");
    HALF_SOUTH = lookupValue(HALF, "south");
    HALF_EAST = lookupValue(HALF, "east");
    HALF_WEST = lookupValue(HALF, "west");

    OPEN_TRUE = lookupValue(OPEN, "true");
    OPEN_FALSE = lookupValue(OPEN, "false");
    POWERED_TRUE = lookupValue(POWERED, "true");
    POWERED_FALSE = lookupValue(POWERED, "false");
    LIT_TRUE = lookupValue(LIT, "true");
    LIT_FALSE = lookupValue(LIT, "false");
    WATERLOGGED_TRUE = lookupValue(WATERLOGGED, "true");
    WATERLOGGED_FALSE = lookupValue(WATERLOGGED, "false");
    LEVEL_0 = lookupValue(LEVEL, "0");
    LEVEL_1 = lookupValue(LEVEL, "1");
    LEVEL_2 = lookupValue(LEVEL, "2");
    LEVEL_3 = lookupValue(LEVEL, "3");
    LEVEL_4 = lookupValue(LEVEL, "4");
    LEVEL_5 = lookupValue(LEVEL, "5");
    LEVEL_6 = lookupValue(LEVEL, "6");
    LEVEL_7 = lookupValue(LEVEL, "7");
    FALLING_TRUE = lookupValue(FALLING, "true");
    FALLING_FALSE = lookupValue(FALLING, "false");
    SHAPE_STRAIGHT = lookupValue(SHAPE, "straight");
    SHAPE_INNER_LEFT = lookupValue(SHAPE, "inner_left");
    SHAPE_INNER_RIGHT = lookupValue(SHAPE, "inner_right");
    SHAPE_OUTER_LEFT = lookupValue(SHAPE, "outer_left");
    SHAPE_OUTER_RIGHT = lookupValue(SHAPE, "outer_right");
    PART_HEAD = lookupValue(PART, "head");
    PART_FOOT = lookupValue(PART, "foot");
    NORTH_TRUE = lookupValue(NORTH, "true");
    NORTH_FALSE = lookupValue(NORTH, "false");
    SOUTH_TRUE = lookupValue(SOUTH, "true");
    SOUTH_FALSE = lookupValue(SOUTH, "false");
    EAST_TRUE = lookupValue(EAST, "true");
    EAST_FALSE = lookupValue(EAST, "false");
    WEST_TRUE = lookupValue(WEST, "true");
    WEST_FALSE = lookupValue(WEST, "false");
    EXTENDED_TRUE = lookupValue(EXTENDED, "true");
    EXTENDED_FALSE = lookupValue(EXTENDED, "false");
    POWER_0 = lookupValue(POWER, "0");
    POWER_1 = lookupValue(POWER, "1");
    POWER_2 = lookupValue(POWER, "2");
    POWER_3 = lookupValue(POWER, "3");
    POWER_4 = lookupValue(POWER, "4");
    POWER_5 = lookupValue(POWER, "5");
    POWER_6 = lookupValue(POWER, "6");
    POWER_7 = lookupValue(POWER, "7");
    POWER_8 = lookupValue(POWER, "8");
    POWER_9 = lookupValue(POWER, "9");
    POWER_10 = lookupValue(POWER, "10");
    POWER_11 = lookupValue(POWER, "11");
    POWER_12 = lookupValue(POWER, "12");
    POWER_13 = lookupValue(POWER, "13");
    POWER_14 = lookupValue(POWER, "14");
    POWER_15 = lookupValue(POWER, "15");
    TYPE_NORMAL = lookupValue(TYPE, "normal");
    TYPE_STICKY = lookupValue(TYPE, "sticky");
    DELAY_1 = lookupValue(DELAY, "1");
    DELAY_2 = lookupValue(DELAY, "2");
    DELAY_3 = lookupValue(DELAY, "3");
    DELAY_4 = lookupValue(DELAY, "4");
    MODE_COMPARE = lookupValue(MODE, "compare");
    MODE_SUBTRACT = lookupValue(MODE, "subtract");
}

}
