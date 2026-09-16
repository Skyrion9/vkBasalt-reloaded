project('zstd', 'c', version: '1.5.7', default_options: ['c_std=c11'])

zstd_inc = include_directories('lib', 'lib/common', 'lib/decompress')

zstd_sources = files(
    'lib/common/entropy_common.c',
    'lib/common/error_private.c',
    'lib/common/fse_decompress.c',
    'lib/common/xxhash.c',
    'lib/common/zstd_common.c',
    'lib/decompress/huf_decompress.c',
    'lib/decompress/zstd_ddict.c',
    'lib/decompress/zstd_decompress.c',
    'lib/decompress/zstd_decompress_block.c',
)

zstd_args = [
    '-DZSTD_LIB_COMPRESSION=0',
    '-DZSTD_LIB_DICTBUILDER=0',
    '-DZSTD_LIB_DEPRECATED=0',
    '-DZSTD_DISABLE_ASM=0',
    '-fno-lto',
    '-fdata-sections',
    '-ffunction-sections',
]

zstd_lib = static_library('zstd',
    zstd_sources,
    include_directories: zstd_inc,
    c_args: zstd_args,
    override_options: ['b_lto=false'],
)

libzstd_dep = declare_dependency(
    link_with: zstd_lib,
    include_directories: zstd_inc,
)
