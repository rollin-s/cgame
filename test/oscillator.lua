-- a silly test system

local tbl = {}
local time = 0

cgame.add_system('test',
{
    update_all = function (dt)
        time = time + dt

        for ent, osc in pairs(tbl) do
            local rate = osc.rate
            if not rate then rate = 2 end

            pos = cgame.transform_get_position(ent)
            pos.x = osc.initx + math.sin(rate * time)
            cgame.transform_set_position(ent, pos)
        end
    end,
})

return
{
    set = function (ent, osc)
        osc.initx = cgame.transform_get_position(ent).x
        tbl[ent] = osc
    end,

    reset = function ()
        time = 0
    end,
}
