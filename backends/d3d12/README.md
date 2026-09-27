# D3D12 backend

`jfx_backend_d3d12` provides D3D12 backend selection and the shared validated
CPU execution contract on every host. Its capability query deliberately does
not advertise GPU support until adapter selection, DXIL compilation, descriptor
heaps, command lists, and fences are implemented against the Windows SDK.
