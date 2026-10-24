-- NanoC for servers that want TLS on the external client bridge.
-- Build from the repository root:
--
--   sudo luarocks make tls/nanoc-0.1.0-2.rockspec --lua-version 5.1
--
-- Requires OpenSSL development files (libssl-dev / openssl-devel).
-- The default rockspec (../nanoc-0.1.0-1.rockspec) builds without TLS.

package = "nanoc"
version = "0.1.0-2"
source = {
    url = "git+https://github.com/joe7575/nanoc_lib.git"
}

description = {
    summary = "NanoC compiler/VM with source-level debugger for Luanti (TLS build)",
    detailed = [[
        A small C-like language, compiler and virtual machine with a
        source-level debugger, packaged as a Lua C module for use in
        Luanti/Minetest computer mods. This revision links OpenSSL and
        enables TLS for the external client bridge.
    ]],
    homepage = "https://github.com/joe7575/nanoc_lib",
    license = "AGPL-3.0-or-later"
}

dependencies = {
   "lua == 5.1"
}

external_dependencies = {
    OPENSSL = {
        header = "openssl/ssl.h",
        library = "ssl",
    },
}

build = {
    type = "builtin",
    modules = {
        nanoclib = {
            incdirs = { "./src", "$(OPENSSL_INCDIR)" },
            libdirs = { "$(OPENSSL_LIBDIR)" },
            defines = { "NANOC_TLS" },
            libraries = { "ssl", "crypto" },
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
