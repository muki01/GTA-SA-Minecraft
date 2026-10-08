#include <d3d9.h>
#include <cstdio>
int main() {
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { printf("Direct3DCreate9 failed\n"); return 1; }
    UINT adapters = d3d->GetAdapterCount();
    printf("adapters: %u\n", adapters);
    for (UINT a = 0; a < adapters; ++a) {
        D3DADAPTER_IDENTIFIER9 id; d3d->GetAdapterIdentifier(a, 0, &id);
        D3DDISPLAYMODE cur; d3d->GetAdapterDisplayMode(a, &cur);
        printf("adapter %u: %s  current %ux%u fmt %d @%u\n", a, id.Description, cur.Width, cur.Height, cur.Format, cur.RefreshRate);
        D3DFORMAT fmts[] = { D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_A2R10G10B10 };
        for (D3DFORMAT f : fmts) {
            UINT n = d3d->GetAdapterModeCount(a, f);
            printf("  format %d: %u modes\n", f, n);
            if (f != D3DFMT_X8R8G8B8) continue;
            for (UINT i = 0; i < n; ++i) {
                D3DDISPLAYMODE m; d3d->EnumAdapterModes(a, f, i, &m);
                printf("    [%2u] %ux%u @%u\n", i, m.Width, m.Height, m.RefreshRate);
            }
        }
    }
    d3d->Release();
    return 0;
}
