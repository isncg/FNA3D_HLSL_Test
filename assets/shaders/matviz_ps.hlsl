// Matrix visualizer pixel shader — renders a 4x4 grayscale grid.
// Each cell (row, col) = saturate(MatrixTransform[row][col]).
// UV.x selects column, UV.y selects row.

float4x4 MatrixTransform : register(c0);

float4 PSMain(float2 uv : TEXCOORD0) : SV_TARGET0
{
    int col = int(floor(uv.x * 4.0));
    int row = int(floor(uv.y * 4.0));

    col = clamp(col, 0, 3);
    row = clamp(row, 0, 3);

    float val = MatrixTransform[row][col];
    val = saturate(val);

    return float4(val, val, val, 1.0);
}
