self.description = "-Rs test (recurse through needed optdeps)"

p1 = pmpkg("dummy")
p1.optdepends = ["dep: for foobar"]
self.addpkg2db("local", p1)

p2 = pmpkg("keep")
p2.optdepends = ["dep: for baz"]
self.addpkg2db("local", p2)

p3 = pmpkg("dep")
p3.reason = 1
self.addpkg2db("local", p3)

self.args = "-Rs --recurse-optdeps %s" % p1.name

self.addrule("PACMAN_RETCODE=0")
self.addrule("!PKG_EXIST=dummy")
self.addrule("!PKG_EXIST=dep")
self.addrule("PKG_EXIST=keep")
