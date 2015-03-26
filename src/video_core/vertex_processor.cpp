// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/range/algorithm/fill.hpp>

#include "common/profiler.h"

#include "pica.h"
#include "vertex_processor.h"
#include "vertex_shader.h"
#include "debug_utils/debug_utils.h"

Common::Profiling::TimingCategory category_drawing("Drawing");

namespace Pica {

namespace VertexProcessor {

void ProcessBatch(bool is_indexed, PrimitiveAssembler<VertexShader::OutputVertex>::TriangleHandler triangle_handler) {
    Common::Profiling::ScopeTimer scope_timer(category_drawing);

    DebugUtils::DumpTevStageConfig(registers.GetTevStages());

    if (g_debug_context)
        g_debug_context->OnEvent(DebugContext::Event::IncomingPrimitiveBatch, nullptr);

    const auto& attribute_config = registers.vertex_attributes;
    const u32 base_address = attribute_config.GetPhysicalBaseAddress();

    // Information about internal vertex attributes
    u32 vertex_attribute_sources[16];
    boost::fill(vertex_attribute_sources, 0xdeadbeef);
    u32 vertex_attribute_strides[16];
    u32 vertex_attribute_formats[16];

    // HACK: Initialize vertex_attribute_elements to zero to prevent infinite loops below.
    // This is one of the hacks required to deal with uninitalized vertex attributes.
    // TODO: Fix this properly.
    u32 vertex_attribute_elements[16] = {};
    u32 vertex_attribute_element_size[16];

    // Setup attribute data from loaders
    for (int loader = 0; loader < 12; ++loader) {
        const auto& loader_config = attribute_config.attribute_loaders[loader];

        u32 load_address = base_address + loader_config.data_offset;

        // TODO: What happens if a loader overwrites a previous one's data?
        for (unsigned component = 0; component < loader_config.component_count; ++component) {
            u32 attribute_index = loader_config.GetComponent(component);
            vertex_attribute_sources[attribute_index] = load_address;
            vertex_attribute_strides[attribute_index] = static_cast<u32>(loader_config.byte_count);
            vertex_attribute_formats[attribute_index] = static_cast<u32>(attribute_config.GetFormat(attribute_index));
            vertex_attribute_elements[attribute_index] = attribute_config.GetNumElements(attribute_index);
            vertex_attribute_element_size[attribute_index] = attribute_config.GetElementSizeInBytes(attribute_index);
            load_address += attribute_config.GetStride(attribute_index);
        }
    }

    // Load vertices
    const auto& index_info = registers.index_array;
    const u8* index_address_8 = Memory::GetPointer(PAddrToVAddr(base_address + index_info.offset));
    const u16* index_address_16 = (u16*)index_address_8;
    bool index_u16 = index_info.format != 0;

    DebugUtils::GeometryDumper geometry_dumper;
    PrimitiveAssembler<VertexShader::OutputVertex> clipper_primitive_assembler(registers.triangle_topology.Value());
    PrimitiveAssembler<DebugUtils::GeometryDumper::Vertex> dumping_primitive_assembler(registers.triangle_topology.Value());

    for (unsigned int index = 0; index < registers.num_vertices; ++index)
    {
        unsigned int vertex = is_indexed ? (index_u16 ? index_address_16[index] : index_address_8[index]) : index;

        if (is_indexed) {
            // TODO: Implement some sort of vertex cache!
        }

        // Initialize data for the current vertex
        VertexShader::InputVertex input;

        // Load a debugging token to check whether this gets loaded by the running
        // application or not.
        static const float24 debug_token = float24::FromRawFloat24(0x00abcdef);
        input.attr[0].w = debug_token;

        for (int i = 0; i < attribute_config.GetNumTotalAttributes(); ++i) {
            for (unsigned int comp = 0; comp < vertex_attribute_elements[i]; ++comp) {
                const u8* srcdata = Memory::GetPointer(PAddrToVAddr(vertex_attribute_sources[i] + vertex_attribute_strides[i] * vertex + comp * vertex_attribute_element_size[i]));

                // TODO(neobrain): Ocarina of Time 3D has GetNumTotalAttributes return 8,
                // yet only provides 2 valid source data addresses. Need to figure out
                // what's wrong there, until then we just continue when address lookup fails
                if (srcdata == nullptr)
                    continue;

                const float srcval = (vertex_attribute_formats[i] == 0) ? *(s8*)srcdata :
                    (vertex_attribute_formats[i] == 1) ? *(u8*)srcdata :
                    (vertex_attribute_formats[i] == 2) ? *(s16*)srcdata :
                    *(float*)srcdata;
                input.attr[i][comp] = float24::FromFloat32(srcval);
                LOG_TRACE(HW_GPU, "Loaded component %x of attribute %x for vertex %x (index %x) from 0x%08x + 0x%08lx + 0x%04lx: %f",
                    comp, i, vertex, index,
                    attribute_config.GetPhysicalBaseAddress(),
                    vertex_attribute_sources[i] - base_address,
                    vertex_attribute_strides[i] * vertex + comp * vertex_attribute_element_size[i],
                    input.attr[i][comp].ToFloat32());
            }
        }

        // HACK: Some games do not initialize the vertex position's w component. This leads
        //       to critical issues since it messes up perspective division. As a
        //       workaround, we force the fourth component to 1.0 if we find this to be the
        //       case.
        //       To do this, we additionally have to assume that the first input attribute
        //       is the vertex position, since there's no information about this other than
        //       the empiric observation that this is usually the case.
        if (input.attr[0].w == debug_token)
            input.attr[0].w = float24::FromFloat32(1.0);

        if (g_debug_context)
            g_debug_context->OnEvent(DebugContext::Event::VertexLoaded, (void*)&input);

        // NOTE: When dumping geometry, we simply assume that the first input attribute
        //       corresponds to the position for now.
        DebugUtils::GeometryDumper::Vertex dumped_vertex = {
            input.attr[0][0].ToFloat32(), input.attr[0][1].ToFloat32(), input.attr[0][2].ToFloat32()
        };
        using namespace std::placeholders;
        dumping_primitive_assembler.SubmitVertex(dumped_vertex,
            std::bind(&DebugUtils::GeometryDumper::AddTriangle,
            &geometry_dumper, _1, _2, _3));

        // Send to vertex shader
        VertexShader::OutputVertex output = VertexShader::RunShader(input, attribute_config.GetNumTotalAttributes());

        if (is_indexed) {
            // TODO: Add processed vertex to vertex cache!
        }

        // Send to triangle clipper
        clipper_primitive_assembler.SubmitVertex(output, triangle_handler);
    }
    geometry_dumper.Dump();

    if (g_debug_context)
        g_debug_context->OnEvent(DebugContext::Event::FinishedPrimitiveBatch, nullptr);
}

} // namespace

} // namespace
