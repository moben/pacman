self.description = "Query unneeded deps (installed optdeps not required)"

pkg = pmpkg("dummy")
pkg.optdepends = ["dep: for foobar"]
self.addpkg2db("local", pkg)

dep = pmpkg("dep")
self.addpkg2db("local", dep)
dep.reason = 1

self.args = "-Qtdn"

self.addrule("PACMAN_RETCODE=0")
self.addrule("PACMAN_OUTPUT=^dep.*dummy")
