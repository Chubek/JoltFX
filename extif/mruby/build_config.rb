MRuby::Build.new do |conf|
  conf.toolchain
  conf.cc.flags << '-Wno-unused-but-set-variable'
  conf.cc.flags += ENV.fetch('JFX_MRUBY_WARNING_FLAGS', '').split
  conf.gem core: 'mruby-compiler'
end

MRuby::CrossBuild.new('jfx') do |conf|
  conf.toolchain :gcc
  conf.cc.defines << 'MRB_NO_STDIO'
  conf.cc.defines << 'MRB_USE_DEBUG_HOOK'
  conf.cc.defines << 'MRB_INT64'
  conf.cc.defines << 'MRB_WORDBOX_NO_FLOAT_TRUNCATE'
  conf.cc.flags << '-Wno-unused-but-set-variable'
  conf.cc.flags += ENV.fetch('JFX_MRUBY_WARNING_FLAGS', '').split
  conf.cc.flags += ENV.fetch('JFX_MRUBY_CFLAGS', '').split
  conf.gembox 'default-no-stdio'
  conf.gem core: 'mruby-compiler'
end
