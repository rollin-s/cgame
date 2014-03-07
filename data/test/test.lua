require 'test.oscillator'
require 'test.rotator'

-- add camera: 32 pixels is one unit

camera = cs.entity.create()

cs.transform.add(camera)
cs.camera.add(camera)
cs.camera.set_viewport_size(camera, cs.game.get_window_size() / 32.0)

-- add some blocks

math.randomseed(os.time())

function symrand()
    return 2 * math.random() - 1
end

local n_blocks = 20
for i = 0, n_blocks do
    local block = cs.entity.create()

    local pos = cg.vec2(i - 8, i - 8)

    cs.transform.add(block)
    cs.transform.set_position(block, pos)

    cs.sprite.add(block)
    if i / 2 == math.floor(i / 2) then
        cs.sprite.set_cell(block, cg.vec2( 0.0, 32.0))
    else
        cs.sprite.set_cell(block, cg.vec2(32.0, 32.0))
    end
    cs.sprite.set_size(block, cg.vec2(32.0, 32.0))

    cs.oscillator.add(block, { amp = 1, freq = 1, phase = 5 * i / n_blocks })
    cs.rotator.add(block, 2 * math.pi)
end

-- add player

player = cg.add
{
    transform = {
        position = cg.vec2(0, 0),
        scale = cg.vec2(2, 2),
        rotation = math.pi / 16,
    },
    sprite = { cell = cg.vec2(0, 32), size = cg.vec2(32, 32) },
}

rchild = cg.add
{
    transform = {
        parent = player,
        position = cg.vec2(1, 0),
        scale = cg.vec2(0.5, 0.5),
    },
    sprite = { cell = cg.vec2(32, 32), size = cg.vec2(32, 32) },
}

lchild = cg.add
{
    transform = {
        parent = player,
        position = cg.vec2(-1, 0),
        scale = cg.vec2(0.5, 0.5),
    },
    sprite = { cell = cg.vec2(32, 32), size = cg.vec2(32, 32) },
}

-- who gets keyboard?

--cs.keyboard.controlled_add(camera)
cs.keyboard.controlled_add(player)

-- entity destruction

-- for each key 1 .. 9 pressed, destroy that entity
cs.destroyer = {}
function cs.destroyer.update_all()
    for i = 1, 9 do
        if cs.input.key_down('KC_' .. i) then
            cs.entity.destroy(cg.Entity { id = i })
        end
    end
end
