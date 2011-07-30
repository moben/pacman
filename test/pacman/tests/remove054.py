self.description = "-Rss test (unneeded optdeps, explicit)"

p1 = pmpkg("dummy")
p1.optdepends = ["dep: for foobar"]
self.addpkg2db("local", p1)

p2 = pmpkg("dep")
self.addpkg2db("local", p2)

self.args = "-Rss %s" % p1.name

self.addrule("PACMAN_RETCODE=0")
self.addrule("!PKG_EXIST=dummy")
self.addrule("!PKG_EXIST=dep")
