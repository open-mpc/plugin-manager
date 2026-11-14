-- openmpc_pm_launch: Lua device script that starts the OpenMPC Plugin Manager
-- background daemon at MPC boot (Gen2 / MPC OS 3.10.x). MPC's Lua VM has io=nil,
-- os=nil; package.loadlib works. The .so lives next to this script.
local function find_module()
    if debug and debug.getinfo then
        local src = debug.getinfo(1, "S").source
        if src and src:sub(1,1) == "@" then
            local dir = src:sub(2):match("(.*/)")
            if dir then return dir .. "openmpc_pm_launch.so" end
        end
    end
    for _, root in ipairs({"/nvme", "/sdcard", "/data/UserData"}) do
        local path = root .. "/MPC Documents/Devices/OpenMPC PM Server/openmpc_pm_launch.so"
        if package.loadlib(path, "*") then return path end
    end
    return nil
end
local function log(m) if print then pcall(print, "[openmpc-pm-launch] " .. tostring(m)) end end
local function run()
    if not package or type(package.loadlib) ~= "function" then return "no-loadlib" end
    local mod = find_module()
    if not mod then return "no-module" end
    local fn = package.loadlib(mod, "openmpc_pm_launch_check")
    if type(fn) ~= "function" then return "load-failed" end
    local ok, res = pcall(fn)
    return "done:" .. tostring(ok) .. ":" .. tostring(res)
end
log("script loaded")
log("direct: " .. tostring(run()))
if openmpc_pm_launch ~= nil then
    function openmpc_pm_launch.OnLoad() return run() end
    function openmpc_pm_launch.OnUnload() end
end
return "helper-registered"
