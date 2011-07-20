self.description = "Install a package from a sync db with uninstalled optdepend and optdepend output"

pkg = pmpkg("dummy")
pkg.optdepends = ["dep: for foobar"]
self.addpkg2db("sync1", pkg)

self.args = "-S %s" % pkg.name

self.addrule("PACMAN_RETCODE=0")
self.addrule("PACMAN_OUTPUT=dep: for foobar")
self.addrule("PKG_EXISTS=dummy")
self.addrule("PKG_OPTDEPENDS=dummy|dep: for foobar")
