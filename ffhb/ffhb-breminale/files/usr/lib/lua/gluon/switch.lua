local util = require 'luci.util'

module('gluon.switch', package.seeall)
function get_data()
    local port = -1
    local ret = {}
    for _, line in ipairs(util.split(util.exec("swconfig dev switch0 show"))) do
        local match = line:match("^Port (%d):$")
        if match then
            port = match+0
        elseif port > 0 then
            if not ret["port"..port] then
                ret["port"..port] = {}
            end
            for k, v in line:gmatch("(%S+)%s+:%s+(%d+)") do
                if v ~= "0" then
                    ret["port"..port][k] = v+0
                end
            end
            if line:match("^%s+link: ") then
                for k, v in line:gmatch("(%S+):(%S+)") do
                    if k ~= "port" then
                        ret["port"..port][k] = v
                    end
                end
            end
        end
    end
    return ret
end
