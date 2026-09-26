# Kernel compilation will be handled by Zoltan

# Custom target to validate kernels (requires Zoltan)
add_custom_target(validate_kernels
    COMMAND echo "Kernel validation requires Zoltan compiler"
    SOURCES ${KERNEL_SOURCES}
)
