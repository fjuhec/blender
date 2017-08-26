import logging
import json
from pathlib import Path
from . import exceptions
from . import utils

class Package:
    """
    Stores package methods and metadata
    """

    log = logging.getLogger(__name__ + ".Package")

    def __init__(self, package_dict:dict = None):
        self.bl_info = {}
        self.url     = ""
        self.files   = []

        self.repositories = set()
        self.installed_location = None
        self.module_name = None

        self.installed = False
        self.is_user = False
        self.enabled = False

        self.set_from_dict(package_dict)

    def test_is_user(self) -> bool:
        """Return true if package's install location is in user or preferences scripts path"""
        import bpy
        user_script_path = bpy.utils.script_path_user()
        prefs_script_path = bpy.utils.script_path_pref()

        if user_script_path is not None:
            in_user = Path(user_script_path) in Path(self.installed_location).parents
        else:
            in_user = False

        if prefs_script_path is not None:
            in_prefs = Path(prefs_script_path) in Path(self.installed_location).parents
        else:
            in_prefs = False

        return in_user or in_prefs

    def test_enabled(self) -> bool:
        """Return true if package is enabled"""
        import bpy
        if self.module_name is not None:
            return (self.module_name in bpy.context.user_preferences.addons)
        else:
            return False

    def test_installed(self) -> bool:
        """Return true if package is installed"""
        import addon_utils
        return len([Package.from_module(mod) for mod in addon_utils.modules(refresh=False) if
                addon_utils.module_bl_info(mod)['name'] == self.name and
                addon_utils.module_bl_info(mod)['version'] == self.version]) > 0

    def set_installed_metadata(self, installed_pkg):
        """Sets metadata specific to installed packages from the Package given as `installed_pkg`"""
        self.installed = installed_pkg.test_installed()
        self.enabled = installed_pkg.test_enabled()
        self.is_user = installed_pkg.test_is_user()
        self.module_name = installed_pkg.module_name
        self.installed_location = installed_pkg.installed_location

    def to_dict(self) -> dict:
        """
        Return a dict representation of the package
        """
        return {
                'bl_info': self.bl_info,
                'url': self.url,
                'files': self.files,
                }

    def set_from_dict(self, package_dict: dict):
        """
        Get attributes from a dict such as produced by `to_dict`
        """
        if package_dict is None:
            package_dict = {}
        
        for attr in ('files', 'url', 'bl_info'):
            if package_dict.get(attr) is not None:
                setattr(self, attr, package_dict[attr])

    # bl_info convenience getters {{{
    # required fields
    @property
    def name(self) -> str:
        """Get name from bl_info"""
        return self.bl_info.get('name')

    @property
    def version(self) -> tuple:
        """Get version from bl_info"""
        return tuple(self.bl_info.get('version'))

    @property
    def blender(self) -> tuple:
        """Get blender from bl_info"""
        return self.bl_info.get('blender')

    # optional fields
    @property
    def description(self) -> str:
        """Get description from bl_info"""
        return self.bl_info.get('description')

    @property
    def author(self) -> str:
        """Get author from bl_info"""
        return self.bl_info.get('author')

    @property
    def category(self) -> str:
        """Get category from bl_info"""
        return self.bl_info.get('category')

    @property
    def location(self) -> str:
        """Get location from bl_info"""
        return self.bl_info.get('location')

    @property
    def support(self) -> str:
        """Get support from bl_info"""
        return self.bl_info.get('support')

    @property
    def warning(self) -> str:
        """Get warning from bl_info"""
        return self.bl_info.get('warning')

    @property
    def wiki_url(self) -> str:
        """Get wiki_url from bl_info"""
        return self.bl_info.get('wiki_url')

    @property
    def tracker_url(self) -> str:
        """Get tracker_url from bl_info"""
        return self.bl_info.get('tracker_url')
    # }}}

    # @classmethod
    # def from_dict(cls, package_dict: dict):
    #     """
    #     Return a Package with values from dict
    #     """
    #     pkg = cls()
    #     pkg.set_from_dict(package_dict)

    @classmethod
    def from_blinfo(cls, blinfo: dict):
        """
        Return a Package with bl_info filled in
        """
        return cls({'bl_info': blinfo})

    @classmethod
    def from_module(cls, module):
        """
        Return a Package object from an addon module
        """
        from pathlib import Path
        filepath = Path(module.__file__)
        if filepath.name == '__init__.py':
            filepath = filepath.parent

        pkg = cls()
        pkg.files = [filepath.name]
        pkg.installed_location = str(filepath)
        pkg.module_name = module.__name__

        try:
            pkg.bl_info = module.bl_info
        except AttributeError as err:
            raise exceptions.BadAddon("Module does not appear to be an addon; no bl_info attribute") from err
        return pkg

    def download(self, dest: Path, progress_callback=None) -> Path:
        """Downloads package to `dest`"""

        if not self.url:
            raise ValueError("Cannot download package without a URL")
        
        return utils.download(self.url, dest, progress_callback)

    def install(self, dest_dir: Path, cache_dir: Path, progress_callback=None):
        """Downloads package to `cache_dir`, then extracts/moves package to `dest_dir`"""

        log = logging.getLogger('%s.install' % __name__)

        downloaded = self.download(cache_dir, progress_callback)

        if not downloaded:
            log.debug('Download returned None, not going to install anything.')
            return

        utils.install(downloaded, dest_dir)
        # utils.rm(downloaded)

    def __eq__(self, other):
        return self.name == other.name and self.version == other.version

    def __lt__(self, other):
        return self.version < other.version

    def __hash__(self):
        return hash((self.name, self.version))

    def __repr__(self) -> str:
        # return self.name
        return "Package('name': {}, 'version': {})".format(self.name, self.version)

class ConsolidatedPackage:
    """
    Stores a grouping of different versions of the same package
    """

    log = logging.getLogger(__name__ + ".ConsolidatedPackage")

    def __init__(self, pkg=None):
        self.versions = []
        self.updateable = False

        if pkg is not None:
            self.add_version(pkg)

    @property
    def installed(self) -> bool:
        """Return true if any version of this package is installed"""
        for pkg in self.versions:
            if pkg.installed:
                return True
        return False

    @property
    def name(self) -> str:
        """
        Return name of this package. All package versions in a
        ConsolidatedPackage should have the same name by definition

        Returns None if there are no versions
        """
        try:
            return self.versions[0].name
        except IndexError:
            return None

    def get_latest_installed_version(self) -> Package:
        """
        Return the installed package with the highest version number.
        If no packages are installed, return None.
        """
        #self.versions is always sorted newer -> older, so we can just grab the first we find
        for pkg in self.versions:
            if pkg.installed:
                return pkg 
        return None

    def get_latest_version(self) -> Package:
        """Return package with highest version number, returns None if there are no versions"""
        try:
            return self.versions[0] # this is always sorted with the highest on top
        except IndexError:
            return None

    def get_display_version(self) -> Package:
        """
        Return installed package with highest version number.
        If no version is installed, return highest uninstalled version.
        """
        pkg = self.get_latest_installed_version()
        if pkg is None:
            pkg = self.get_latest_version()
        return pkg

    def add_version(self, newpkg: Package):
        """Adds a package to the collection of versions"""

        if self.name and newpkg.name != self.name:
            raise exceptions.PackageException("Name mismatch, refusing to add %s to %s" % (newpkg, self))

        for pkg in self:
            if pkg == newpkg:
                pkg.repositories.union(newpkg.repositories)
                if newpkg.installed:
                    pkg.set_installed_metadata(newpkg)
                return

        self.versions.append(newpkg)
        self.versions.sort(key=lambda v: v.version, reverse=True)


    def __iter__(self):
        return (pkg for pkg in self.versions)

    def __repr__(self):
        return ("ConsolidatedPackage<name={}>".format(self.name))

class Repository:
    """
    Stores repository metadata (including packages)
    """

    log = logging.getLogger(__name__ + ".Repository")

    def __init__(self, url=None):
        if url is None:
            url = ""
        self.set_from_dict({'url': url})

    # def cleanse_packagelist(self):
    #     """Remove empty packages (no bl_info), packages with no name"""

    def refresh(self, storage_path: Path, progress_callback=None):
        """
        Requests repo.json from URL and embeds etag/last-modification headers
        """
        import requests

        if progress_callback is None:
            progress_callback = lambda x: None

        progress_callback(0.0)

        if self.url is None:
            raise ValueError("Cannot refresh repository without a URL")

        url = utils.add_repojson_to_url(self.url)

        self.log.debug("Refreshing repository from %s", self.url)

        req_headers = {}
        # Do things this way to avoid adding empty objects/None to the req_headers dict
        try:
            req_headers['If-None-Match'] = self._headers['etag']
        except KeyError:
            pass
        try:
            req_headers['If-Modified-Since'] = self._headers['last-modified']
        except KeyError:
            pass

        try:
            resp = requests.get(url, headers=req_headers, timeout=60)
        except requests.exceptions.InvalidSchema as err:
            raise exceptions.DownloadException("Invalid schema. Did you mean to use http://?") from err
        except requests.exceptions.ConnectionError as err:
            raise exceptions.DownloadException("Failed to connect. Are you sure '%s' is the correct URL?" % url) from err
        except requests.exceptions.RequestException as err:
            raise exceptions.DownloadException(err) from err

        try:
            resp.raise_for_status()
        except requests.HTTPError as err:
            self.log.error('Error downloading %s: %s', url, err)
            raise exceptions.DownloadException(resp.status_code, resp.reason) from err

        if resp.status_code == requests.codes.not_modified:
            self.log.debug("Packagelist not modified")
            return

        resp_headers = {}
        try:
            resp_headers['etag'] = resp.headers['etag']
        except KeyError:
            pass
        try:
            resp_headers['last-modified'] = resp.headers['last-modified']
        except KeyError:
            pass

        self.log.debug("Found headers: %s", resp_headers)

        progress_callback(0.7)
        
        try:
            repodict = resp.json()
        except json.decoder.JSONDecodeError:
            self.log.exception("Failed to parse downloaded repository")
            raise exceptions.BadRepositoryException(
                    "Could not parse repository downloaded from '%s'. Are you sure this is the correct URL?" % url
                    )
        repodict['_headers'] = resp_headers
        repodict['url'] = self.url

        self.set_from_dict(repodict)
        self.to_file(storage_path / utils.format_filename(self.name, ".json"))

        progress_callback(1.0)


    def to_dict(self, sort=False, ids=False) -> dict:
        """
        Return a dict representation of the repository
        """
        packages = [p.to_dict() for p in self.packages]

        if sort:
            packages.sort(key=lambda p: p['bl_info']['name'].lower())

        if ids:
            for pkg in packages:
                # hash may be too big for a C int
                pkg['id'] = str(hash(pkg['url'] + pkg['bl_info']['name'] + self.name + self.url))

        return {
                'name':     self.name,
                'packages': packages,
                'url':      self.url,
                '_headers': self._headers,
                }

    def set_from_dict(self, repodict: dict):
        """
        Get repository attributes from a dict such as produced by `to_dict`
        """

        # def initialize(item, value):
        #     if item is None:
        #         return value
        #     else:
        #         return item

        #Be certain to initialize everything; downloaded packagelist might contain null values
        # url      = initialize(repodict.get('url'), "")
        # packages = initialize(repodict.get('packages'), [])
        # headers  = initialize(repodict.get('_headers'), {})
        name = repodict.get('name', "")
        url      = repodict.get('url', "")
        packages = repodict.get('packages', [])
        headers  = repodict.get('_headers', {})

        self.name = name
        self.url = url
        self.packages = [Package(pkg) for pkg in packages]
        self._headers = headers

    @classmethod
    def from_dict(cls, repodict: dict):
        """
        Like `set_from_dict`, but immutable
        """
        repo = cls()
        repo.set_from_dict(repodict)
        return repo

    def to_file(self, path: Path):
        """
        Dump repository to a json file at `path`.
        """
        if self.packages is None:
            self.log.warning("Writing an empty repository")

        self.log.debug("URL is %s", self.url)

        with path.open('w', encoding='utf-8') as repo_file:
            json.dump(self.to_dict(), repo_file, indent=4, sort_keys=True)
        self.log.debug("Repository written to %s" % path)

    # def set_from_file(self, path: Path):
    #     """
    #     Set the current instance's attributes from a json file
    #     """
    #     repo_file = path.open('r', encoding='utf-8')
    #
    #     with repo_file:
    #         try:
    #             self.set_from_dict(json.load(repo_file))
    #         except Exception as err:
    #             raise BadRepository from err
    #
    #     self.log.debug("Repository read from %s", path)

    @classmethod
    def from_file(cls, path: Path):
        """
        Read repository from a json file at `path`.
        """
        repo_file = path.open('r', encoding='utf-8')

        with repo_file:
            try:
                repo = cls.from_dict(json.load(repo_file))
            except json.JSONDecodeError as err:
                raise exceptions.BadRepositoryException(err) from err
            if repo.url is None or len(repo.url) == 0:
                raise exceptions.BadRepositoryException("Repository missing URL")

        cls.log.debug("Repository read from %s", path)
        return repo

    def __repr__(self):
        return "Repository({}, {})".format(self.name, self.url)
