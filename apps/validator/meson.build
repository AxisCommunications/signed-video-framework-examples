gstapp_dep = dependency(
  'gstreamer-app-@0@'.format(api_version),
  version : gst_req,
)

validator_sources = files(
  'main.c'
)

executable('validator',
  validator_sources,
  build_rpath : sv_lib_dir,
  install_rpath : sv_lib_dir,
  dependencies : [ signedvideoframework_dep, gst_dep, gstapp_dep ],
  install : true,
)
