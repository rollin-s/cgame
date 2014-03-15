GRID_SIZE = 32.0 / 3.0
WIN_WIDTH = 800
WIN_HEIGHT = 600

math.randomseed(os.time())

function symrand()
    return 2 * math.random() - 1
end

cs.physics.set_simulation_frequency(300)

-- add camera

camera = cg.add {
    camera = { viewport_size = cs.game.get_window_size() / GRID_SIZE }
}

-- add floor

local D = 16

floor = cg.add {
    transform = { position = cg.vec2_zero, scale = cg.vec2(32, 32) },
    sprite = { cell = cg.vec2(0, 0), size = cg.vec2(32, 32) },
    physics = { type = cs.PB.KINEMATIC },
    keyboard_controlled = {},
}

cs.physics.add_box_shape(floor, -D, -D, D, -(D - 1))
cs.physics.add_box_shape(floor, -D, D - 1, D, D)
cs.physics.add_box_shape(floor, -D, -D, -(D - 1), D)
cs.physics.add_box_shape(floor, D - 1, -D, D, D)

-- oscillator_set(floor, { amp = 8, freq = 0.5 })
-- rotator_set(floor, 0.1 * math.pi)

-- add some boxes

boxes = {}
function make_box(pos, vel)
    local box = cg.add {
        transform = { position = pos },
        sprite = { cell = cg.vec2(32, 32), size = cg.vec2(32, 32) },
        physics = { type = cg.PB_DYNAMIC, mass = 5, velocity = vel },
    }
    cs.physics.add_box_shape(box, -0.5, -0.5, 0.5, 0.5)

    boxes[box] = true
end

function clear_boxes()
    for e, _ in pairs(boxes) do cs.entity.destroy(e) end
    boxes = {}
end

local ymin = -3.0
local ymax = 8.0

local y = ymin
while y < ymax do
    local xspan = 9.0 * (1.0 - (y - ymin) / (ymax - ymin))
    local x = -xspan + math.random()
    while x < xspan do
        make_box(cg.vec2(x, y), cg.vec2(symrand(), symrand()))
        x = x + 1.0 + 3.0 * math.random()
    end
    y = y + 1.2
end

-- add more boxes with 'B'

cs.box_add = {}
function cs.box_add.update_all()
    if cs.input.key_down('KC_B') then
        make_box(cg.vec2(5 * symrand(), 5 * symrand()),
                 cg.vec2(5 * symrand(), 5 * symrand()))
    end
end

-- keys

cs.keys = {}
function cs.keys.update_all()
    -- press a number to make it dynamic, hold right shift for static,
    -- left shift for kinematic
    for i = 1, 9 do
        if cs.input.key_down('KC_' .. i) then
            if cs.input.key_down(cs.KC.LEFT_SHIFT) then
                cs.physics.set_type(i, cs.PB.KINEMATIC)
            elseif cs.input.key_down(cs.KC.RIGHT_SHIFT) then
                cs.physics.set_type(i, cs.PB.STATIC)
            else
                cs.entity.destroy(i)
                -- cs.physics.set_type(i, cs.PB.DYNAMIC)
            end
        end
    end

    -- pause/resume
    if cs.input.key_down(cs.KC.U) then
        cs.timing.set_paused(true)
    elseif cs.input.key_down(cs.KC.I) then
        cs.timing.set_paused(false)
    end
end

cs.click_destroy = {}
function cs.click_destroy.update_all()
    if cs.input.mouse_down(cs.MC.LEFT) then
        p = cs.input.get_mouse_pos_unit()
        p = cs.transform.local_to_world(camera, p)

        r = cs.physics.nearest(p, 1)
        if r.ent == cg.entity_nil then
            print('nothing!')
        elseif r.d <= 0 then
            cs.entity.destroy(r.ent)
        end
    end
end

print('this is the physics test')
