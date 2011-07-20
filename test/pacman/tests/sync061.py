self.description = "Install a package from a sync db with installed optdepend and no optdepend output"

p1 = pmpkg("dummy")
p1.optdepends = ["dep: for foobar"]
self.addpkg2db("sync1", p1)

p2 = pmpkg("dep")
self.addpkg2db("local", p2)

self.args = "-S %s" % p1.name

self.addrule("PACMAN_RETCODE=0")
self.addrule("!PACMAN_OUTPUT=dep: for foobar")
self.addrule("PKG_EXISTS=dummy")
self.addrule("PKG_OPTDEPENDS=dummy|dep: for foobar")
