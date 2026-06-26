#pragma once

#include <cstdint>
#include <limits>

namespace PropIndices {

constexpr uint16_t INVALID = std::numeric_limits<uint16_t>::max();

extern uint16_t FACING;
extern uint16_t AXIS;
extern uint16_t HALF;
extern uint16_t OPEN;
extern uint16_t ENABLED;
extern uint16_t POWERED;
extern uint16_t LIT;
extern uint16_t WATERLOGGED;
extern uint16_t LEVEL;
extern uint16_t FALLING;
extern uint16_t SHAPE;
extern uint16_t PART;
extern uint16_t NORTH;
extern uint16_t SOUTH;
extern uint16_t EAST;
extern uint16_t WEST;
extern uint16_t EXTENDED;
extern uint16_t POWER;
extern uint16_t TYPE;
extern uint16_t DELAY;
extern uint16_t MODE;
extern uint16_t LOCKED;

extern uint16_t FACING_FLOOR;
extern uint16_t FACING_NORTH;
extern uint16_t FACING_SOUTH;
extern uint16_t FACING_EAST;
extern uint16_t FACING_WEST;
extern uint16_t FACING_UP;
extern uint16_t FACING_DOWN;

extern uint16_t AXIS_X;
extern uint16_t AXIS_Y;
extern uint16_t AXIS_Z;

extern uint16_t HALF_TOP;
extern uint16_t HALF_BOTTOM;
extern uint16_t HALF_DOUBLE;
extern uint16_t HALF_NORTH;
extern uint16_t HALF_SOUTH;
extern uint16_t HALF_EAST;
extern uint16_t HALF_WEST;

extern uint16_t OPEN_TRUE;
extern uint16_t OPEN_FALSE;
extern uint16_t ENABLED_TRUE;
extern uint16_t ENABLED_FALSE;
extern uint16_t POWERED_TRUE;
extern uint16_t POWERED_FALSE;
extern uint16_t LIT_TRUE;
extern uint16_t LIT_FALSE;
extern uint16_t WATERLOGGED_TRUE;
extern uint16_t WATERLOGGED_FALSE;
extern uint16_t LEVEL_0;
extern uint16_t LEVEL_1;
extern uint16_t LEVEL_2;
extern uint16_t LEVEL_3;
extern uint16_t LEVEL_4;
extern uint16_t LEVEL_5;
extern uint16_t LEVEL_6;
extern uint16_t LEVEL_7;
extern uint16_t FALLING_TRUE;
extern uint16_t FALLING_FALSE;
extern uint16_t SHAPE_STRAIGHT;
extern uint16_t SHAPE_INNER_LEFT;
extern uint16_t SHAPE_INNER_RIGHT;
extern uint16_t SHAPE_OUTER_LEFT;
extern uint16_t SHAPE_OUTER_RIGHT;
extern uint16_t PART_HEAD;
extern uint16_t PART_FOOT;
extern uint16_t NORTH_TRUE;
extern uint16_t NORTH_FALSE;
extern uint16_t SOUTH_TRUE;
extern uint16_t SOUTH_FALSE;
extern uint16_t EAST_TRUE;
extern uint16_t EAST_FALSE;
extern uint16_t WEST_TRUE;
extern uint16_t WEST_FALSE;
extern uint16_t NORTH_NONE;
extern uint16_t NORTH_SIDE;
extern uint16_t NORTH_UP;
extern uint16_t SOUTH_NONE;
extern uint16_t SOUTH_SIDE;
extern uint16_t SOUTH_UP;
extern uint16_t EAST_NONE;
extern uint16_t EAST_SIDE;
extern uint16_t EAST_UP;
extern uint16_t WEST_NONE;
extern uint16_t WEST_SIDE;
extern uint16_t WEST_UP;
extern uint16_t EXTENDED_TRUE;
extern uint16_t EXTENDED_FALSE;
extern uint16_t POWER_0;
extern uint16_t POWER_1;
extern uint16_t POWER_2;
extern uint16_t POWER_3;
extern uint16_t POWER_4;
extern uint16_t POWER_5;
extern uint16_t POWER_6;
extern uint16_t POWER_7;
extern uint16_t POWER_8;
extern uint16_t POWER_9;
extern uint16_t POWER_10;
extern uint16_t POWER_11;
extern uint16_t POWER_12;
extern uint16_t POWER_13;
extern uint16_t POWER_14;
extern uint16_t POWER_15;
extern uint16_t TYPE_NORMAL;
extern uint16_t TYPE_STICKY;
extern uint16_t DELAY_1;
extern uint16_t DELAY_2;
extern uint16_t DELAY_3;
extern uint16_t DELAY_4;
extern uint16_t MODE_COMPARE;
extern uint16_t MODE_SUBTRACT;
extern uint16_t LOCKED_TRUE;
extern uint16_t LOCKED_FALSE;

void init();

}
