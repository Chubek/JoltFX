MRuby::Build.new do |conf|
  conf.toolchain
  conf.gem core: 'mruby-compiler'
end

MRuby::CrossBuild.new('jfx') do |conf|
  conf.toolchain :gcc
  conf.cc.defines << 'MRB_NO_STDIO'
  conf.cc.defines << 'MRB_USE_DEBUG_HOOK'
  conf.gembox 'default-no-stdio'
  conf.gem core: 'mruby-compiler'
end
