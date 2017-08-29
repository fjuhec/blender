# HACK:
# due to lack of fork() on windows, multiprocessing will re-execute this module
# in a new process. In such cases we only need subproc, everything else is only
# used to spawn the subprocess in the first place.
try:
    import bpy
    from bpy.types import Operator
except ImportError:
    from bpkg import subproc
else:
    import logging
    import bpkg
    from bpkg import (
        subproc,
        messages,
    )
    from bpkg.types import (
        Package,
        ConsolidatedPackage,
    )
    from pathlib import Path
    from collections import OrderedDict
    import multiprocessing

    # Under windows, multiprocessing must start a new process entirely. It
    # expects sys.executable to point to python, but in blender sys.executable
    # points to blender's executable. We can override this with set_executable,
    # but this acts globally unless we make a special context.
    # Also see:
    # https://docs.python.org/3.6/library/multiprocessing.html#multiprocessing.set_executable
    mp_context = multiprocessing.get_context()
    mp_context.set_executable(bpy.app.binary_path_python)

    class SubprocMixin:
        """Mix-in class for things that need to be run in a subprocess."""

        log = logging.getLogger(__name__ + '.SubprocMixin')
        _state = 'INITIALIZING'
        _abort_timeout = 0  # time at which we stop waiting for an abort response and just terminate the process

        # Mapping from message type (see bpkg.messages) to handler function.
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

    class PACKAGE_OT_install(SubprocMixin, Operator):
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

            package = bpkg.list_packages()[self.package_name].get_latest_version()

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
            bpkg.tag_reindex()
            bpy.context.area.tag_redraw()
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

    class PACKAGE_OT_uninstall(SubprocMixin, Operator):
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
            bpy.context.area.tag_redraw()
            bpkg.tag_reindex()
            self.quit()

        def report_process_died(self):
            if self.process.exitcode:
                self.log.error('Process died without telling us! Exit code was %i', self.process.exitcode)
                self.report({'ERROR'}, 'Error downloading package, exit code %i' % self.process.exitcode)
            else:
                self.log.error('Process died without telling us! Exit code was 0 though')
                self.report({'WARNING'}, 'Error downloading package, but process finished OK. This is weird.')


    class PACKAGE_OT_refresh(SubprocMixin, Operator):
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
                bpkg.tag_reindex()
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
            bpkg.refresh_repository_props()
            bpkg.tag_reindex()
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

    class PACKAGE_UL_repositories(bpy.types.UIList):
        def draw_item(self, context, layout, data, item, icon, active_data, active_propname):
            layout.alignment='LEFT'
            layout.prop(item, "enabled", text="")
            if len(item.name) == 0:
                layout.label(item['url'])
            else:
                layout.label(item.name)

    class PACKAGE_OT_add_repository(Operator):
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

            for repo in wm.package_repositories:
                if repo['url'] == self.url:
                    self.report({'ERROR'}, "Repository already added")
                    return {'CANCELLED'}

            repo = wm.package_repositories.add()
            repo.url = bpkg.utils.sanitize_repository_url(self.url)

            # bpy.ops.package.refresh()
            context.area.tag_redraw()
            return {'FINISHED'}

    class PACKAGE_OT_remove_repository(Operator):
        bl_idname = "package.remove_repository"
        bl_label = "Remove Repository"

        def execute(self, context):
            wm = context.window_manager
            try:
                repo = wm.package_repositories[wm.package_active_repository]
            except AttributeError:
                return {'CANCELLED'}

            try:
                filepath = Path(repo['filepath'])
            except KeyError:
                pass
            else:
                if not filepath.exists():
                    raise ValueError("Failed find repository file")
                filepath.unlink()

            wm.package_repositories.remove(wm.package_active_repository)
            # bpy.ops.package.refresh()
            context.area.tag_redraw()
            return {'FINISHED'}

    class PACKAGE_OT_edit_repositories(Operator):
        bl_idname = "package.edit_repositories"
        bl_label = "Edit Repositories"

        def check(self, context):
            # TODO: always refresh settings for now
            return True

        def execute(self, context):
            bpy.ops.package.refresh()
            return {'FINISHED'}

        def invoke(self, context, event):
            wm = context.window_manager
            return wm.invoke_props_dialog(self, width=500, height=300)

        def draw(self, context):
            layout = self.layout
            wm = context.window_manager

            row = layout.row()
            row.template_list("PACKAGE_UL_repositories", "", wm, "package_repositories", wm, "package_active_repository")
            col = row.column(align=True)
            col.operator("package.add_repository", text="", icon='ZOOMIN')
            col.operator("package.remove_repository", text="", icon='ZOOMOUT')


    class WM_OT_package_toggle_expand(Operator):
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
                bpkg.display.expanded_packages.clear()
            if self.package_name in bpkg.display.expanded_packages:
                bpkg.display.expanded_packages.remove(self.package_name)
            else:
                bpkg.display.expanded_packages.append(self.package_name)

            return {'FINISHED'}

    class WM_OT_package_toggle_preferences(Operator):
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
            return {'FINISHED'}

    class PACKAGE_OT_toggle_enabled(Operator):
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
            metapkg = bpkg.list_packages()[self.package_name]


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

            return {'FINISHED'}

    class PACKAGE_OT_disable(Operator):
        bl_idname = "package.disable"
        bl_label = ""
        bl_description = "Disable given package"

        log = logging.getLogger(__name__ + ".PACKAGE_OT_disable")

        package_name = bpy.props.StringProperty(
                name="Package Name",
                description="Name of package to disable",
                )

        def execute(self, context):
            package = bpkg.list_packages()[self.package_name].get_display_version()

            if not package.module_name:
                self.log.error("Can't disable package without a module name")
                return {'CANCELLED'}

            ret = bpy.ops.wm.addon_disable(package.module_name)
            if ret == {'FINISHED'}:
                bpkg.list_packages()[self.package_name].enabled = False
            return ret

classes = (
    PACKAGE_OT_install,
    PACKAGE_OT_uninstall,
    PACKAGE_OT_toggle_enabled,
    PACKAGE_OT_refresh,
    WM_OT_package_toggle_expand,
    WM_OT_package_toggle_preferences,
    PACKAGE_OT_add_repository,
    PACKAGE_OT_remove_repository,
    PACKAGE_OT_edit_repositories,
    PACKAGE_UL_repositories,
)
