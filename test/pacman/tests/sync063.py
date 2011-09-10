self.description = "Install a package from a sync db with automatic optdepend install"

self.option["HandleOptdeps"] = ["Install"]

p1 = pmpkg("dummy")
p1.optdepends = ["dep: for foobar"]
self.addpkg2db("sync1", p1)

p2 = pmpkg("dep")
self.addpkg2db("sync1", p2)

self.args = "-S %s" % p1.name

self.addrule("PACMAN_RETCODE=0")
self.addrule("PKG_EXIST=dummy")
self.addrule("PKG_EXIST=dep")
self.addrule("PKG_OPTDEPENDS=dummy|dep: for foobar")
