# HACK:
# due to lack of fork() on windows, multiprocessing will re-execute this module
# in a new process. In such cases we only need subproc, everything else is only
# used to spawn the subprocess in the first place.
try:
    import bpy
except ImportError:
    from bpkg import subproc
else:
    import logging
    import bpkg
    from bpkg import (
        subproc,
        messages,
        utils,
    )
    from bpkg.types import (
        Package,
        ConsolidatedPackage,
    )
    from pathlib import Path
    from collections import OrderedDict
    import multiprocessing

    mp_context = multiprocessing.get_context()
    mp_context.set_executable(bpy.app.binary_path_python)

    # global list of all known packages, indexed by name
    # _packages = OrderedDict()

    # used for lazy loading
    _main_has_run = False


    class SubprocMixin:
        """Mix-in class for things that need to be run in a subprocess."""

        log = logging.getLogger(__name__ + '.SubprocMixin')
        _state = 'INITIALIZING'
        _abort_timeout = 0  # time at which we stop waiting for an abort response and just terminate the process

        # Mapping from message type (see bpkg_manager.subproc) to handler function.
        # Should be constructed before modal() gets called.
        msg_handlers = {}

        def execute(self, context):
            return self.invoke(context, None)

        def quit(self):
            """Signals the state machine to stop this operator from running."""
            self._state = 'QUIT'

        def invoke(self, context, event):
            self.pipe_blender, self.pipe_subproc = multiprocessing.Pipe()

            # The subprocess should just be terminated when Blender quits. Without this,
            # Blender would hang while closing, until the subprocess terminates itself.
            # TODO: Perhaps it would be better to fork when blender exits?
            self.process = self.create_subprocess()
            self.process.daemon = True
            self.process.start()

            self._state = 'RUNNING'

            wm = context.window_manager
            wm.modal_handler_add(self)
            self.timer = wm.event_timer_add(0.1, context.window)

            return {'RUNNING_MODAL'}

        def modal(self, context, event):
            import time

            if event.type == 'ESC':
                self.log.warning('Escape pressed, sending abort signal to subprocess')
                self.abort()
                return {'PASS_THROUGH'}

            if event.type != 'TIMER':
                return {'PASS_THROUGH'}

            if self._state == 'ABORTING' and time.time() > self._abort_timeout:
                self.log.error('No response from subprocess to abort request, terminating it.')
                self.report({'ERROR'}, 'No response from subprocess to abort request, terminating it.')
                self.process.terminate()
                self._finish(context)
                return {'CANCELLED'}

            while self.pipe_blender.poll():
                self.handle_received_data()

            if self._state == 'QUIT':
                self._finish(context)
                return {'FINISHED'}

            if not self.process.is_alive():
                self.report_process_died()
                self._finish(context)
                return {'CANCELLED'}

            return {'RUNNING_MODAL'}

        def abort(self):
            import time

            # Allow the subprocess 10 seconds to repsond to our abort message.
            self._abort_timeout = time.time() + 10
            self._state = 'ABORTING'

            self.pipe_blender.send(messages.Abort())

        def _finish(self, context):
            try:
                self.cancel(context)
            except AttributeError:
                pass

            global bpkg_operation_running

            context.window_manager.event_timer_remove(self.timer)
            bpkg_operation_running = False

            if self.process and self.process.is_alive():
                self.log.debug('Waiting for subprocess to quit')
                try:
                    self.process.join(timeout=10)
                except multiprocessing.TimeoutError:
                    self.log.warning('Subprocess is hanging, terminating it forcefully.')
                    self.process.terminate()
                else:
                    self.log.debug('Subprocess stopped with exit code %i', self.process.exitcode)

        def handle_received_data(self):
            recvd = self.pipe_blender.recv()

            self.log.debug('Received message from subprocess: %s', recvd)
            try:
                handler = self.msg_handlers[type(recvd)]
            except KeyError:
                self.log.error('Unable to handle received message %s', recvd)
                # Maybe we shouldn't show this to the user?
                self.report({'WARNING'}, 'Unable to handle received message %s' % recvd)
                return

            handler(recvd)

        def create_subprocess(self):
            """Implement this in a subclass.

            :rtype: multiprocessing.Process
            """
            raise NotImplementedError()

        def report_process_died(self):
            """Provides the user with sensible information when the process has died.

            Implement this in a subclass.
            """
            raise NotImplementedError()


    class PACKAGE_OT_install(SubprocMixin, bpy.types.Operator):
        bl_idname = 'package.install'
        bl_label = 'Install package'
        bl_description = 'Downloads and installs a Blender add-on package'
        bl_options = {'REGISTER'}

        package_name = bpy.props.StringProperty(
                name='package_name',
                description='The name of the package to install'
                )

        log = logging.getLogger(__name__ + '.PACKAGE_OT_install')

        def invoke(self, context, event):
            if not self.package_name:
                self.report({'ERROR'}, 'Package name not given')
                return {'CANCELLED'}

            return super().invoke(context, event)

        def create_subprocess(self):
            """Starts the download process.

            Also registers the message handlers.

            :rtype: multiprocessing.Process
            """

            self.msg_handlers = {
                messages.Progress: self._subproc_progress,
                messages.DownloadError: self._subproc_download_error,
                messages.InstallError: self._subproc_install_error,
                messages.Success: self._subproc_success,
                messages.Aborted: self._subproc_aborted,
            }

            package = bpkg.packages[self.package_name].get_latest_version()

            import pathlib

            # TODO: We need other paths besides this one on subprocess end, so it might be better to pass them all at once.
            # For now, just pass this one.
            install_path = pathlib.Path(bpy.utils.user_resource('SCRIPTS', 'addons', create=True))
            self.log.debug("Using %s as install path", install_path)

            import addon_utils
            proc = mp_context.Process(target=subproc.download_and_install_package,
                                           args=(self.pipe_subproc, package, install_path))
            return proc

        def _subproc_progress(self, progress: messages.Progress):
            self.log.info('Task progress at %i%%', progress.progress * 100)

        def _subproc_download_error(self, error: messages.DownloadError):
            self.report({'ERROR'}, 'Unable to download package: %s' % error.message)
            self.quit()

        def _subproc_install_error(self, error: messages.InstallError):
            self.report({'ERROR'}, 'Unable to install package: %s' % error.message)
            self.quit()

        def _subproc_success(self, success: messages.Success):
            self.report({'INFO'}, 'Package installed successfully')
            self.quit()

        def _subproc_aborted(self, aborted: messages.Aborted):
            self.report({'ERROR'}, 'Package installation aborted per your request')
            self.quit()

        def report_process_died(self):
            if self.process.exitcode:
                self.log.error('Process died without telling us! Exit code was %i', self.process.exitcode)
                self.report({'ERROR'}, 'Error downloading package, exit code %i' % self.process.exitcode)
            else:
                self.log.error('Process died without telling us! Exit code was 0 though')
                self.report({'WARNING'}, 'Error downloading package, but process finished OK. This is weird.')

    class PACKAGE_OT_uninstall(SubprocMixin, bpy.types.Operator):
        bl_idname = 'package.uninstall'
        bl_label = 'Install package'
        bl_description = "Remove installed package files from filesystem"
        bl_options = {'REGISTER'}

        package_name = bpy.props.StringProperty(name='package_name', description='The name of the package to uninstall')

        log = logging.getLogger(__name__ + '.PACKAGE_OT_uninstall')

        def invoke(self, context, event):
            if not self.package_name:
                self.report({'ERROR'}, 'Package name not given')
                return {'CANCELLED'}

            return super().invoke(context, event)

        def create_subprocess(self):
            """Starts the uninstall process and registers the message handlers.
            :rtype: multiprocessing.Process
            """

            self.msg_handlers = {
                messages.UninstallError: self._subproc_uninstall_error,
                messages.Success: self._subproc_success,
            }

            import pathlib
            install_path = pathlib.Path(bpy.utils.user_resource('SCRIPTS', 'addons', create=True))

            package = bpkg.list_packages()[self.package_name].get_latest_version()

            proc = mp_context.Process(target=subproc.uninstall_package,
                                           args=(self.pipe_subproc, package, install_path))
            return proc


        def _subproc_uninstall_error(self, error: messages.InstallError):
            self.report({'ERROR'}, error.message)
            self.quit()

        def _subproc_success(self, success: messages.Success):
            self.report({'INFO'}, 'Package uninstalled successfully')
            bpy.ops.package.refresh()
            self.quit()

        def report_process_died(self):
            if self.process.exitcode:
                self.log.error('Process died without telling us! Exit code was %i', self.process.exitcode)
                self.report({'ERROR'}, 'Error downloading package, exit code %i' % self.process.exitcode)
            else:
                self.log.error('Process died without telling us! Exit code was 0 though')
                self.report({'WARNING'}, 'Error downloading package, but process finished OK. This is weird.')


    # class PACKAGE_OT_refresh_packages(bpy.types.Operator):
    #     bl_idname = "package.refresh_packages"
    #     bl_label = "Refresh Packages"
    #     bl_description = "Scan for packages on disk"
    #
    #     log = logging.getLogger(__name__ + ".PACKAGE_OT_refresh_packages")
    #
    #     def execute(self, context):
    #         global _packages
    #         installed_packages = get_packages_from_disk(refresh=True)
    #         available_packages = get_packages_from_repo()
    #         _packages = build_composite_packagelist(installed_packages, available_packages)
    #         context.area.tag_redraw()
    #
    #         return {'FINISHED'}

    class PACKAGE_OT_refresh(SubprocMixin, bpy.types.Operator):
        bl_idname = "package.refresh"
        bl_label = "Refresh"
        bl_description = 'Check repositories for new and updated packages'
        bl_options = {'REGISTER'}

        log = logging.getLogger(__name__ + ".PACKAGE_OT_refresh")
        _running = False

        def invoke(self, context, event):
            wm = context.window_manager
            self.repositories = wm.package_repositories
            if len(self.repositories) == 0:
                self.report({'ERROR'}, "No repositories to refresh")
                return {'CANCELLED'}

            PACKAGE_OT_refresh._running = True
            return super().invoke(context, event)

        @classmethod
        def poll(cls, context):
            return not cls._running

        def cancel(self, context):
            PACKAGE_OT_refresh._running = False
            context.area.tag_redraw()

        def create_subprocess(self):
            """Starts the download process.

            Also registers the message handlers.

            :rtype: multiprocessing.Process
            """

            #TODO: make sure all possible messages are handled
            self.msg_handlers = {
                messages.Progress: self._subproc_progress,
                messages.SubprocError: self._subproc_error,
                messages.DownloadError: self._subproc_download_error,
                messages.Success: self._subproc_success,
                # messages.RepositoryResult: self._subproc_repository_result,
                messages.BadRepositoryError: self._subproc_repository_error,
                messages.Aborted: self._subproc_aborted,
            }

            import pathlib

            storage_path = pathlib.Path(bpy.utils.user_resource('CONFIG', 'repositories', create=True))
            repository_urls = [repo.url for repo in self.repositories]
            self.log.debug("Repository urls %s", repository_urls)

            proc = mp_context.Process(target=subproc.refresh_repositories,
                                           args=(self.pipe_subproc, storage_path, repository_urls))
            return proc

        def _subproc_progress(self, progress: messages.Progress):
            self.log.info('Task progress at %i%%', progress.progress * 100)

        def _subproc_error(self, error: messages.SubprocError):
            self.report({'ERROR'}, 'Unable to refresh package list: %s' % error.message)
            self.quit()

        def _subproc_download_error(self, error: messages.DownloadError):
            self.report({'ERROR'}, 'Unable to download package list: %s' % error.message)
            self.quit()

        def _subproc_repository_error(self, error: messages.BadRepositoryError):
            self.report({'ERROR'}, str(error.message))
            self.quit()

        def _subproc_success(self, success: messages.Success):
            self.report({'INFO'}, 'Finished refreshing lists')
            self.quit()

        def _subproc_aborted(self, aborted: messages.Aborted):
            self.report({'ERROR'}, 'Package list retrieval aborted per your request')
            self.quit()

        def report_process_died(self):
            if self.process.exitcode:
                self.log.error('Refresh process died without telling us! Exit code was %i', self.process.exitcode)
                self.report({'ERROR'}, 'Error refreshing package lists, exit code %i' % self.process.exitcode)
            else:
                self.log.error('Refresh process died without telling us! Exit code was 0 though')
                self.report({'WARNING'}, 'Error refreshing package lists, but process finished OK. This is weird.')

    class RepositoryProperty(bpy.types.PropertyGroup):
        name = bpy.props.StringProperty(name="Name")
        url = bpy.props.StringProperty(name="URL")
        status = bpy.props.EnumProperty(name="Status", items=[
                ("OK",        "Okay",              "FILE_TICK"),
                ("NOTFOUND",  "Not found",         "ERROR"),
                ("NOCONNECT", "Could not connect", "QUESTION"),
                ])
        enabled = bpy.props.BoolProperty(name="Enabled")

    class PACKAGE_UL_repositories(bpy.types.UIList):
        def draw_item(self, context, layout, data, item, icon, active_data, active_propname):
            layout.alignment='LEFT'
            layout.prop(item, "enabled", text="")
            if len(item.name) == 0:
                layout.label(item['url'])
            else:
                layout.label(item.name)

    class PACKAGE_OT_add_repository(bpy.types.Operator):
        bl_idname = "package.add_repository"
        bl_label = "Add Repository"

        url = bpy.props.StringProperty(name="Repository URL")

        def invoke(self, context, event):
            wm = context.window_manager
            return wm.invoke_props_dialog(self)

        def execute(self, context):
            wm = context.window_manager

            if len(self.url) == 0:
                self.report({'ERROR'}, "Repository URL not specified")
                return {'CANCELLED'}

            repo = wm.package_repositories.add()
            repo.url = utils.sanitize_repository_url(self.url)

            bpy.ops.package.refresh()

            context.area.tag_redraw()
            return {'FINISHED'}

    class PACKAGE_OT_remove_repository(bpy.types.Operator):
        bl_idname = "package.remove_repository"
        bl_label = "Remove Repository"

        def execute(self, context):
            wm = context.window_manager
            try:
                repo = wm['package_repositories'][wm.package_active_repository]
            except IndexError:
                return {'CANCELLED'}

            filename = utils.format_filename(repo.name) + ".json"
            path = (utils.get_repo_storage_path() / filename)
            if path.exists():
                path.unlink()

            wm.package_repositories.remove(wm.package_active_repository)

            return {'FINISHED'}

    # class USERPREF_PT_packages(bpy.types.Panel):
    #     bl_label = "Package Management"
    #     bl_space_type = 'USER_PREFERENCES'
    #     bl_region_type = 'WINDOW'
    #     bl_options = {'HIDE_HEADER'}
    #
    #     log = logging.getLogger(__name__ + '.USERPREF_PT_packages')
    #
    #     displayed_packages = []
    #     expanded_packages = []
    #     preference_package = None
    #
    #     redraw = True
    #
    #     @classmethod
    #     def poll(cls, context):
    #         userpref = context.user_preferences
    #         return (userpref.active_section == 'PACKAGES')
    #
    #     def draw(self, context):
    #         layout = self.layout
    #         wm = context.window_manager
    #
    #         mainrow = layout.row()
    #         spl = mainrow.split(.2)
    #         sidebar = spl.column(align=True)
    #         pkgzone = spl.column()
    #
    #         sidebar.label("Repositories")
    #         row = sidebar.row()
    #         row.template_list("PACKAGE_UL_repositories", "", wm, "package_repositories", wm, "package_active_repository")
    #         col = row.column(align=True)
    #         col.operator(PACKAGE_OT_add_repository.bl_idname, text="", icon='ZOOMIN')
    #         col.operator(PACKAGE_OT_remove_repository.bl_idname, text="", icon='ZOOMOUT')
    #         sidebar.separator()
    #         sidebar.operator(PACKAGE_OT_refresh.bl_idname, text="Check for updates")
    #
    #         sidebar.separator()
    #         sidebar.label("Category")
    #         sidebar.prop(wm, "addon_filter", text="")
    #
    #         sidebar.separator()
    #         sidebar.label("Support level")
    #         sidebar.prop(wm, "addon_support")
    #
    #         top = pkgzone.row()
    #         spl = top.split(.6)
    #         spl.prop(wm, "package_search", text="", icon='VIEWZOOM')
    #         spl_r = spl.row()
    #         spl_r.prop(wm, "package_install_filter", expand=True)
    #
    #         def filtered_packages(filters: dict, packages: OrderedDict) -> list:# {{{
    #             """Returns filtered and sorted list of names of packages which match filters"""
    #
    #             #TODO: using lower() for case-insensitive comparison doesn't work in some languages
    #             def match_contains(blinfo) -> bool:
    #                 if blinfo['name'].lower().__contains__(filters['search'].lower()):
    #                     return True
    #                 return False
    #
    #             def match_startswith(blinfo) -> bool:
    #                 if blinfo['name'].lower().startswith(filters['search'].lower()):
    #                     return True
    #                 return False
    #
    #             def match_support(blinfo) -> bool:
    #                 if 'support' in blinfo:
    #                     if set((blinfo['support'],)).issubset(filters['support']):
    #                         return True
    #                 else:
    #                     if {'COMMUNITY'}.issubset(filters['support']):
    #                         return True
    #                 return False
    #
    #             def match_installstate(metapkg: ConsolidatedPackage) -> bool:
    #                 if filters['installstate'] == 'AVAILABLE':
    #                     return True
    #
    #                 if filters['installstate'] == 'INSTALLED':
    #                     if metapkg.installed:
    #                         return True
    #
    #                 if filters['installstate'] == 'UPDATES':
    #                     if metapkg.installed:
    #                         if metapkg.get_latest_installed_version().version < metapkg.get_latest_version().version:
    #                             return True
    #                 return False
    #
    #             def match_repositories(metapkg) -> bool:
    #                 pkg = metapkg.get_display_version()
    #                 for repo in pkg.repositories:
    #                     if repo.name in filters['repository']:
    #                         return True
    #                 return False
    #
    #             def match_category(blinfo) -> bool:
    #                 if filters['category'].lower() == 'all':
    #                     return True
    #                 if 'category' not in blinfo:
    #                     return False
    #                 if blinfo['category'].lower() == filters['category'].lower():
    #                     return True
    #                 return False
    #
    #
    #             # use two lists as a simple way of putting "matches from the beginning" on top
    #             contains = []
    #             startswith = []
    #
    #             for pkgname, metapkg in packages.items():
    #                 blinfo = metapkg.versions[0].bl_info
    #                 if match_repositories(metapkg)\
    #                 and match_category(blinfo)\
    #                 and match_support(blinfo)\
    #                 and match_installstate(metapkg):
    #                     if len(filters['search']) == 0:
    #                         startswith.append(pkgname)
    #                         continue
    #                     if match_startswith(blinfo):
    #                         startswith.append(pkgname)
    #                         continue
    #                     if match_contains(blinfo):
    #                         contains.append(pkgname)
    #                         continue
    #
    #             return startswith + contains# }}}
    #
    #         def draw_package(metapkg: ConsolidatedPackage, layout: bpy.types.UILayout): #{{{
    #             """Draws the given package"""
    #             pkg = metapkg.get_display_version()
    #
    #             def draw_operators(metapkg, layout): # {{{
    #                 """
    #                 Draws install, uninstall, update, enable, disable, and preferences
    #                 buttons as applicable for the given package
    #                 """
    #                 pkg = metapkg.get_display_version()
    #
    #                 if metapkg.installed:
    #                     if metapkg.updateable:
    #                         layout.operator(
    #                                 PACKAGE_OT_install.bl_idname,
    #                                 text="Update to {}".format(utils.fmt_version(metapkg.get_latest_version().version)),
    #                                 ).package_name=metapkg.name
    #                         layout.separator()
    #
    #                     #TODO: only show preferences button if addon has preferences to show
    #                     if pkg.enabled:
    #                         layout.operator(
    #                                 WM_OT_package_toggle_preferences.bl_idname,
    #                                 text="Preferences",
    #                                 ).package_name=metapkg.name
    #                     layout.operator(
    #                             PACKAGE_OT_uninstall.bl_idname,
    #                             text="Uninstall",
    #                             ).package_name=metapkg.name
    #                 else:
    #                     layout.operator(
    #                             PACKAGE_OT_install.bl_idname,
    #                             text="Install",
    #                             ).package_name=metapkg.name
    #                 # }}}
    #
    #             def draw_preferences(pkg: Package, layout: bpy.types.UILayout):
    #                 """Draw the package's preferences in the given layout"""
    #                 addon_preferences = context.user_preferences.addons[pkg.module_name].preferences
    #                 if addon_preferences is not None:
    #                     draw = getattr(addon_preferences, "draw", None)
    #                     if draw is not None:
    #                         addon_preferences_class = type(addon_preferences)
    #                         box_prefs = layout.box()
    #                         box_prefs.label("Preferences:")
    #                         addon_preferences_class.layout = box_prefs
    #                         try:
    #                             draw(context)
    #                         except:
    #                             import traceback
    #                             traceback.print_exc()
    #                             box_prefs.label(text="Error (see console)", icon='ERROR')
    #                         del addon_preferences_class.layout
    #
    #             def collapsed(metapkg, layout):# {{{
    #                 """Draw collapsed version of package layout"""
    #                 pkg = metapkg.get_display_version()
    #
    #                 # Only 'install' button is shown when package isn't installed,
    #                 # so allow more space for title/description.
    #                 spl = layout.split(.5 if pkg.installed else .8)
    #
    #                 metacol = spl.column(align=True)
    #
    #                 buttonrow = spl.row(align=True)
    #                 buttonrow.alignment = 'RIGHT'
    #
    #                 l1 = metacol.row()
    #                 l2 = metacol.row()
    #
    #                 draw_operators(metapkg, buttonrow)
    #
    #                 if pkg.installed:
    #                     metacol.active = pkg.enabled
    #                     l1.operator(PACKAGE_OT_toggle_enabled.bl_idname,
    #                               icon='CHECKBOX_HLT' if pkg.enabled else 'CHECKBOX_DEHLT',
    #                               text="",
    #                               emboss=False,
    #                               ).package_name = metapkg.name
    #
    #                 if pkg.name:
    #                     l1.label(text=pkg.name)
    #                 if pkg.description:
    #                     l2.label(text=pkg.description)
    #                     l2.enabled = False #Give name more visual weight
    #             # }}}
    #
    #
    #             def expanded(metapkg, layout, layoutbox):# {{{
    #                 """Draw expanded version of package layout"""
    #
    #                 pkg = metapkg.get_display_version()
    #
    #                 metacol = layoutbox.column(align=True)
    #                 row1 = layout.row(align=True)
    #                 row1.operator(PACKAGE_OT_toggle_enabled.bl_idname,
    #                               icon='CHECKBOX_HLT' if pkg.enabled else 'CHECKBOX_DEHLT',
    #                               text="",
    #                               emboss=False,
    #                               ).package_name = metapkg.name
    #                 row1.label(pkg.name)
    #
    #                 if metapkg.installed:
    #                     metacol.active = pkg.enabled
    #                     row1.active = pkg.enabled
    #
    #                 if pkg.description:
    #                     row = metacol.row()
    #                     row.label(pkg.description)
    #
    #                 def draw_metadatum(label: str, value: str, layout: bpy.types.UILayout):
    #                     """Draw the given key value pair in a new row in given layout container"""
    #                     row = layout.row()
    #                     row.scale_y = .8
    #                     spl = row.split(.15)
    #                     spl.label("{}:".format(label))
    #                     spl.label(value)
    #
    #                 # don't compare against None here; we don't want to display empty arrays/strings either
    #                 if pkg.location:
    #                     draw_metadatum("Location", pkg.location, metacol)
    #                 if pkg.version:
    #                     draw_metadatum("Version", utils.fmt_version(pkg.version), metacol)
    #                 if pkg.blender:
    #                     draw_metadatum("Blender version", utils.fmt_version(pkg.blender), metacol)
    #                 if pkg.category:
    #                     draw_metadatum("Category", pkg.category, metacol)
    #                 if pkg.author:
    #                     draw_metadatum("Author", pkg.author, metacol)
    #                 if pkg.support:
    #                     draw_metadatum("Support level", pkg.support.title(), metacol)
    #                 if pkg.warning:
    #                     draw_metadatum("Warning", pkg.warning, metacol)
    #                 
    #                 metacol.separator()
    #
    #                 spl = layoutbox.row().split(.35)
    #                 urlrow = spl.row()
    #                 buttonrow = spl.row(align=True)
    #
    #                 urlrow.alignment = 'LEFT'
    #                 if pkg.wiki_url:
    #                     urlrow.operator("wm.url_open", text="Documentation", icon='HELP').url=pkg.wiki_url
    #                 if pkg.tracker_url:
    #                     urlrow.operator("wm.url_open", text="Report a Bug", icon='URL').url=pkg.tracker_url
    #
    #                 buttonrow.alignment = 'RIGHT'
    #                 draw_operators(metapkg, buttonrow)
    #
    #                 def draw_version(layout: bpy.types.UILayout, pkg: Package):
    #                     """Draw version of package"""
    #                     spl = layout.split(.9)
    #                     left = spl.column()
    #                     right = spl.column()
    #                     right.alignment = 'RIGHT'
    #
    #                     left.label(text=utils.fmt_version(pkg.version))
    #
    #                     for repo in pkg.repositories:
    #                         draw_metadatum("Repository", repo.name, left)
    #
    #                     if pkg.installed:
    #                         right.label(text="Installed")
    #
    #                         draw_metadatum("Installed to", str(pkg.installed_location), left)
    #
    #                 if len(metapkg.versions) > 1:
    #                     row = pkgbox.row()
    #                     row.label(text="There are multiple versions of this package:")
    #                     for version in metapkg.versions:
    #                         subvbox = pkgbox.box()
    #                         draw_version(subvbox, version)
    #
    #             # }}}
    #
    #             is_expanded = (metapkg.name in self.expanded_packages)
    #
    #             pkgbox = layout.box()
    #             row = pkgbox.row(align=True)
    #             row.operator(
    #                     WM_OT_package_toggle_expand.bl_idname,
    #                     icon='TRIA_DOWN' if is_expanded else 'TRIA_RIGHT',
    #                     emboss=False,
    #                     ).package_name=metapkg.name
    #
    #             if is_expanded:
    #                 expanded(metapkg, row, pkgbox)
    #             else:
    #                 collapsed(metapkg, row)# }}}
    #
    #             if pkg.installed and pkg.enabled and pkg.name == USERPREF_PT_packages.preference_package:
    #                 draw_preferences(pkg, pkgbox)
    #
    #
    #         def center_message(layout, msg: str):
    #             """draw a label in the center of an extra-tall row"""
    #             row = layout.row()
    #             row.label(text=msg)
    #             row.alignment='CENTER'
    #             row.scale_y = 10
    #
    #         global _main_has_run
    #         if not _main_has_run:
    #             # TODO: read repository and installed packages synchronously for now;
    #             # can't run an operator from draw code to do async monitoring
    #             main()
    #
    #         global _packages
    #         if len(_packages) == 0:
    #             center_message(pkgzone, "No packages found")
    #             return
    #
    #         wm = bpy.context.window_manager
    #         filters = {
    #                 'category': wm.addon_filter,
    #                 'search': wm.package_search,
    #                 'support': wm.addon_support,
    #                 'repository': set([repo.name for repo in wm.package_repositories if repo.enabled]),
    #                 'installstate': wm.package_install_filter,
    #                 }
    #         USERPREF_PT_packages.displayed_packages = filtered_packages(filters, _packages)
    #
    #         for pkgname in USERPREF_PT_packages.displayed_packages:
    #             row = pkgzone.row()
    #             draw_package(_packages[pkgname], row)


    class WM_OT_package_toggle_expand(bpy.types.Operator):# {{{
        bl_idname = "wm.package_toggle_expand"
        bl_label = ""
        bl_description = "Toggle display of extended information for given package (hold shift to collapse all other packages)"
        bl_options = {'INTERNAL'}

        log = logging.getLogger(__name__ + ".WM_OT_package_toggle_expand")

        package_name = bpy.props.StringProperty(
                name="Package Name",
                description="Name of package to expand/collapse",
                )

        def invoke(self, context, event):
            if event.shift:
                USERPREF_PT_packages.expanded_packages = []
            if self.package_name in USERPREF_PT_packages.expanded_packages:
                USERPREF_PT_packages.expanded_packages.remove(self.package_name)
            else:
                USERPREF_PT_packages.expanded_packages.append(self.package_name)

            return {'FINISHED'}# }}}

    class WM_OT_package_toggle_preferences(bpy.types.Operator):# {{{
        bl_idname = "wm.package_toggle_preferences"
        bl_label = ""
        bl_description = "Toggle display of package preferences"
        bl_options = {'INTERNAL'}

        package_name = bpy.props.StringProperty(
                name="Package Name",
                description="Name of package whos preferences to display",
                )

        def invoke(self, context, event):
            if USERPREF_PT_packages.preference_package == self.package_name:
                USERPREF_PT_packages.preference_package = None
            else:
                USERPREF_PT_packages.preference_package = self.package_name
            return {'FINISHED'}# }}}

    class PACKAGE_OT_toggle_enabled(bpy.types.Operator):# {{{
        bl_idname = "package.toggle_enabled"
        bl_label = ""
        bl_description = "Enable given package if it's disabled, and vice versa if it's enabled"

        log = logging.getLogger(__name__ + ".PACKAGE_OT_toggle_enabled")

        package_name = bpy.props.StringProperty(
                name="Package Name",
                description="Name of package to enable",
                )

        def execute(self, context):
            import addon_utils
            metapkg = bpkg.packages[self.package_name]


            if not metapkg.installed:
                self.report({'ERROR'}, "Can't enable package which isn't installed")
                return {'CANCELLED'}

            # enable/disable all installed versions, just in case there are more than one
            for pkg in metapkg.versions:
                if not pkg.installed:
                    continue
                if not pkg.module_name:
                    self.log.warning("Can't enable package `%s` without a module name", pkg.name)
                    continue

                if pkg.enabled:
                    addon_utils.disable(pkg.module_name, default_set=True)
                    pkg.enabled = False
                    self.log.debug("Disabling")
                else:
                    self.log.debug("Enabling")
                    addon_utils.enable(pkg.module_name, default_set=True)
                    pkg.enabled = True

            return {'FINISHED'}# }}}

    class PACKAGE_OT_disable(bpy.types.Operator):# {{{
        bl_idname = "package.disable"
        bl_label = ""
        bl_description = "Disable given package"

        log = logging.getLogger(__name__ + ".PACKAGE_OT_disable")

        package_name = bpy.props.StringProperty(
                name="Package Name",
                description="Name of package to disable",
                )

        def execute(self, context):
            package = bpkg.packages[self.package_name].get_display_version()

            if not package.module_name:
                self.log.error("Can't disable package without a module name")
                return {'CANCELLED'}

            ret = bpy.ops.wm.addon_disable(package.module_name)
            if ret == {'FINISHED'}:
                bpkg.packages[self.package_name].enabled = False
            return ret# }}}

    # class PackageManagerPreferences(bpy.types.AddonPreferences):
    #     bl_idname = __package__
    #
    #     repositories = bpy.props.CollectionProperty(
    #             type=RepositoryProperty,
    #             name="Repositories",
    #             )
    #     active_repository = bpy.props.IntProperty()


    # def main():
    #     """Entry point; performs initial loading of repositories and installed packages"""
    #     global _packages
    #     global _main_has_run
    #
    #     _packages = build_packagelist()
    #
    #     # load repositories from disk
    #     repos = get_repositories()
    #     wm = bpy.context.window_manager
    #     wm.package_repositories.clear()
    #
    #     #TODO: store repository props in .blend so enabled/disabled state can be remembered
    #     for repo in repos:
    #         repo_prop = wm.package_repositories.add()
    #         repo_prop.name = repo.name
    #         repo_prop.enabled = True  
    #         repo_prop.url = repo.url
    #     
    #     # needed for lazy loading
    #     _main_has_run = True


    # def register():
    #     bpy.utils.register_class(PACKAGE_OT_install)
    #     bpy.utils.register_class(PACKAGE_OT_uninstall)
    #     bpy.utils.register_class(PACKAGE_OT_toggle_enabled)
    #     # bpy.utils.register_class(PACKAGE_OT_disable)
    #     # bpy.utils.register_class(PACKAGE_OT_refresh_repositories)
    #     # bpy.utils.register_class(PACKAGE_OT_refresh_packages)
    #     bpy.utils.register_class(PACKAGE_OT_refresh)
    #     bpy.utils.register_class(USERPREF_PT_packages)
    #     bpy.utils.register_class(WM_OT_package_toggle_expand)
    #     bpy.utils.register_class(WM_OT_package_toggle_preferences)
    #     bpy.types.WindowManager.package_search = bpy.props.StringProperty(
    #             name="Search",
    #             description="Filter packages by name",
    #             options={'TEXTEDIT_UPDATE'}
    #             )
    #     bpy.types.WindowManager.package_install_filter = bpy.props.EnumProperty(
    #         items=[('AVAILABLE', "Available", "All packages in selected repositories"),
    #                ('INSTALLED', "Installed", "All installed packages"),
    #                ('UPDATES', "Updates", "All installed packages for which there is a newer version availabe")
    #                ],
    #         name="Install filter",
    #         default='AVAILABLE',
    #         )
    #
    #     bpy.utils.register_class(RepositoryProperty)
    #     bpy.types.WindowManager.package_repositories = bpy.props.CollectionProperty(
    #             type=RepositoryProperty,
    #             name="Repositories",
    #             )
    #     bpy.types.WindowManager.package_active_repository = bpy.props.IntProperty()
    #     bpy.utils.register_class(PACKAGE_OT_add_repository)
    #     bpy.utils.register_class(PACKAGE_OT_remove_repository)
    #     bpy.utils.register_class(PACKAGE_UL_repositories)
    #
    #     # bpy.utils.register_class(PackageManagerPreferences)
    #
    #
    # def unregister():
    #     bpy.utils.unregister_class(PACKAGE_OT_install)
    #     bpy.utils.unregister_class(PACKAGE_OT_uninstall)
    #     bpy.utils.unregister_class(PACKAGE_OT_toggle_enabled)
    #     # bpy.utils.unregister_class(PACKAGE_OT_disable)
    #     # bpy.utils.unregister_class(PACKAGE_OT_refresh_repositories)
    #     # bpy.utils.unregister_class(PACKAGE_OT_refresh_packages)
    #     bpy.utils.unregister_class(PACKAGE_OT_refresh)
    #     bpy.utils.unregister_class(USERPREF_PT_packages)
    #     bpy.utils.unregister_class(WM_OT_package_toggle_expand)
    #     bpy.utils.unregister_class(WM_OT_package_toggle_preferences)
    #     del bpy.types.WindowManager.package_search
    #     del bpy.types.WindowManager.package_install_filter
    #
    #     bpy.utils.unregister_class(RepositoryProperty)
    #     del bpy.types.WindowManager.package_repositories
    #     del bpy.types.WindowManager.package_active_repository
    #     bpy.utils.unregister_class(PACKAGE_OT_add_repository)
    #     bpy.utils.unregister_class(PACKAGE_OT_remove_repository)
    #     bpy.utils.unregister_class(PACKAGE_UL_repositories)
    #
    #     # bpy.utils.unregister_class(PackageManagerPreferences)

classes = (
    PACKAGE_OT_install,
    PACKAGE_OT_uninstall,
    PACKAGE_OT_toggle_enabled,
    PACKAGE_OT_refresh,
    WM_OT_package_toggle_expand,
    WM_OT_package_toggle_preferences,
    PACKAGE_OT_add_repository,
    PACKAGE_OT_remove_repository,
    PACKAGE_UL_repositories,
)
