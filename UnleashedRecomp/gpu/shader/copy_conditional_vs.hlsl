// [Switch] Round 15, SwitchSubmitTimeCopies: copy_vs for a resolve copy whose texture may turn out unread. The
// render thread writes the word at ConditionAddress just before the frame is submitted: 1 when something can read
// what the copy writes, 0 when the texture is rewritten entirely (or destroyed) first in the same frame. With 0 the
// three vertices land on one point: the triangle has no area and draws no pixel.

struct ConditionalCopyPushConstants
{
    uint ResourceDescriptorIndex;
    uint Padding;
    uint64_t ConditionAddress;
};

[[vk::push_constant]] ConstantBuffer<ConditionalCopyPushConstants> g_PushConstants : register(b3, space4);

void main(in uint vertexId : SV_VertexID, out float4 position : SV_Position, out float2 texCoord : TEXCOORD)
{
    texCoord = float2((vertexId << 1) & 2, vertexId & 2);
    position = float4(texCoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
#ifdef __spirv__
    if (vk::RawBufferLoad<uint>(g_PushConstants.ConditionAddress) == 0)
        position = float4(0.0, 0.0, 0.0, 1.0);
#endif
}
