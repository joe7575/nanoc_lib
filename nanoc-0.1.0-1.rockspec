package = "nanoc"
version = "0.1.0-1"
source = {
    url = "git+https://github.com/joe7575/nanoc_lib.git"
}

description = {
    summary = "NanoC compiler/VM with source-level debugger for Luanti",
    detailed = [[
        A small C-like language, compiler and virtual machine with a
        source-level debugger, packaged as a Lua C module for use in
        Luanti/Minetest computer mods.
    ]],
    homepage = "https://github.com/joe7575/nanoc_lib",
    license = "AGPL-3.0-or-later"
}

dependencies = {
   "lua == 5.1"
}

-- Optional TLS for the external client bridge:
--   NANOC_TLS=1 sudo -E luarocks make --lua-version 5.1
-- Requires OpenSSL headers and libraries (libssl-dev / openssl-devel).
local tls = os.getenv("NANOC_TLS")
local with_tls = tls ~= nil and tls ~= "" and tls ~= "0"
	and tls ~= "false" and tls ~= "off"
local defines, libraries = {}, {}
if with_tls then
	defines = { "NANOC_TLS" }
	libraries = { "ssl", "crypto" }
end

build = {
    type = "builtin",
    modules = {
        nanoclib = {
            incdirs = { "./src" },
            defines = defines,
            libraries = libraries,
            sources = {
                "./src/nc_scanner.c",
                "./src/nc_compiler.c",
                "./src/nc_runtime.c",
                "./src/nc_memory.c",
                "./src/nc_kvstore.c",
                "./src/nanoc_debug.c",
                "./src/nanoc_host.c",
                "./src/nanoc_techage.c",
                "./src/nanoc_net.c",
                "./lua/nanoclib.c"
            }
        }
    }
}
