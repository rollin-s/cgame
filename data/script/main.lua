-- add cgame require path and load cgame
package.path = package.path .. ';' .. cgame_data_path .. '/script/?.lua'
require 'cgame'

-- misc. keys
cs.app = {}
function cs.app.key_down(key)
    if key == cg.KC_ESCAPE then
        cs.game.quit()

    elseif key == cg.KC_E then
        cs.timing.set_paused(not cs.edit.get_enabled())
        cs.edit.set_enabled(not cs.edit.get_enabled())

    elseif key == cg.KC_P then
        cs.timing.set_paused(not cs.timing.get_paused())

    elseif key == cg.KC_C then
        cs.group.destroy('default')
        print("destroyed group 'default'")

    elseif key == cg.KC_S then
        print("saving group 'default' to file '"
                  .. cgame_usr_path .. "test.sav'")
        cs.group.set_save_filter('default', true)
        local s = cs.serializer.open_file(cgame_usr_path .. 'test.sav')
        cs.system.save_all(s)
        cs.serializer.close(s)

    elseif key == cg.KC_L then
        print("loading from file '" .. cgame_usr_path .. "test.sav'")
        local d = cs.deserializer.open_file(cgame_usr_path .. 'test.sav')
        cs.system.load_all(d)
        cs.deserializer.close(d)

    end
end

-- run startup script if specified
if cg.args[1] then dofile(cg.args[1]) end

