#include "luna/middleend/module/module.h"

#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"
#include "luna/middleend/sema/sema.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum LunaModuleVisitState {
    LUNA_MODULE_UNVISITED,
    LUNA_MODULE_VISITING,
    LUNA_MODULE_VISITED
} LunaModuleVisitState;

typedef struct LunaModuleDependency {
    uint32_t target_index;
    const LunaImport *syntax;
} LunaModuleDependency;

typedef struct LunaModuleRecord {
    LunaStringView name;
    LunaSourceSpan declaration_span;
    const LunaProgram *interface_unit;
    const LunaProgram *implementation_unit;
    bool interface_is_metadata;
    const LunaModuleMetadataDependency *metadata_dependencies;
    uint32_t metadata_dependency_count;
    uint64_t metadata_content_hash;
    LunaVector dependencies;
    LunaVector interface_imports;
    LunaVector implementation_imports;
    LunaModuleVisitState visit_state;
    bool reachable;
} LunaModuleRecord;

typedef struct LunaModuleGraph {
    LunaDiagnosticEngine *diagnostics;
    LunaVector modules;
    LunaVector visit_stack;
    LunaVector order;
    bool allocation_failed;
} LunaModuleGraph;

static void luna_module_report_allocation_failure(LunaModuleGraph *graph) {
    if (!graph->allocation_failed) {
        luna_diagnostic_error_plain(
            graph->diagnostics,
            "out of memory while validating the module dependency graph");
        graph->allocation_failed = true;
    }
}

static void luna_module_record_destroy(LunaModuleRecord *record) {
    luna_vector_destroy(&record->dependencies);
    luna_vector_destroy(&record->interface_imports);
    luna_vector_destroy(&record->implementation_imports);
}

static void luna_module_graph_init(LunaModuleGraph *graph,
                                   LunaDiagnosticEngine *diagnostics) {
    graph->diagnostics = diagnostics;
    luna_vector_init(&graph->modules, sizeof(LunaModuleRecord));
    luna_vector_init(&graph->visit_stack, sizeof(uint32_t));
    luna_vector_init(&graph->order, sizeof(uint32_t));
    graph->allocation_failed = false;
}

static void luna_module_graph_destroy(LunaModuleGraph *graph) {
    for (size_t index = 0U; index < graph->modules.length; index += 1U) {
        LunaModuleRecord *record = luna_vector_at(&graph->modules, index);
        luna_module_record_destroy(record);
    }
    luna_vector_destroy(&graph->order);
    luna_vector_destroy(&graph->visit_stack);
    luna_vector_destroy(&graph->modules);
}

static uint32_t luna_module_find_record(const LunaModuleGraph *graph,
                                        LunaStringView name) {
    for (size_t index = 0U; index < graph->modules.length; index += 1U) {
        const LunaModuleRecord *record =
            luna_vector_at_const(&graph->modules, index);
        if (luna_string_view_equal(record->name, name)) {
            return (uint32_t)index;
        }
    }
    return UINT32_MAX;
}

static bool luna_module_add_input(LunaModuleGraph *graph,
                                  const LunaModuleInput *input) {
    const LunaProgram *program = input == NULL ? NULL : input->program;
    if (program == NULL) {
        luna_diagnostic_error_plain(graph->diagnostics,
                                    "module compilation received a null "
                                    "source unit");
        return false;
    }
    if (input->is_metadata && !program->is_interface) {
        luna_diagnostic_error(
            graph->diagnostics, program->module_span,
            "compiled module metadata must contain an interface unit");
        return false;
    }
    if (input->is_metadata && input->metadata_dependency_count > 0U &&
        input->metadata_dependencies == NULL) {
        luna_diagnostic_error(
            graph->diagnostics, program->module_span,
            "compiled module metadata has an invalid dependency table");
        return false;
    }
    if (input->is_metadata) {
        uint32_t dependency_index = 0U;
        for (const LunaImport *import = program->first_import; import != NULL;
             import = import->next) {
            if (dependency_index >= input->metadata_dependency_count) {
                luna_diagnostic_error(
                    graph->diagnostics, program->module_span,
                    "compiled module metadata dependency table does not "
                    "match its imports");
                return false;
            }
            const LunaModuleMetadataDependency *dependency =
                &input->metadata_dependencies[dependency_index];
            if (!luna_string_view_equal(dependency->module_name,
                                        import->module_name)) {
                luna_diagnostic_error(
                    graph->diagnostics, program->module_span,
                    "compiled module metadata dependency table is malformed");
                return false;
            }
            dependency_index += 1U;
        }
        if (dependency_index != input->metadata_dependency_count) {
            luna_diagnostic_error(
                graph->diagnostics, program->module_span,
                "compiled module metadata has unused dependency "
                "fingerprints");
            return false;
        }
    }

    uint32_t module_index =
        luna_module_find_record(graph, program->module_name);
    if (module_index == UINT32_MAX) {
        if (graph->modules.length >= UINT32_MAX) {
            luna_module_report_allocation_failure(graph);
            return false;
        }

        LunaModuleRecord record = {
            .name = program->module_name,
            .declaration_span = program->module_span,
        };
        luna_vector_init(&record.dependencies, sizeof(LunaModuleDependency));
        luna_vector_init(&record.interface_imports, sizeof(LunaSemaImport));
        luna_vector_init(&record.implementation_imports,
                         sizeof(LunaSemaImport));
        if (!luna_vector_push(&graph->modules, &record)) {
            luna_module_record_destroy(&record);
            luna_module_report_allocation_failure(graph);
            return false;
        }
        module_index = (uint32_t)(graph->modules.length - 1U);
    }

    LunaModuleRecord *record =
        luna_vector_at(&graph->modules, (size_t)module_index);
    const LunaProgram **selected = program->is_interface
                                       ? &record->interface_unit
                                       : &record->implementation_unit;
    if (*selected != NULL) {
        luna_diagnostic_error(
            graph->diagnostics, program->module_span,
            "module '%.*s' has more than one %s unit",
            (int)program->module_name.length, program->module_name.data,
            program->is_interface ? "interface" : "implementation");
        luna_diagnostic_note(
            graph->diagnostics, (*selected)->module_span,
            "the first %s unit for module '%.*s' is here",
            program->is_interface ? "interface" : "implementation",
            (int)program->module_name.length, program->module_name.data);
        return false;
    }
    *selected = program;
    if (program->is_interface) {
        record->interface_is_metadata = input->is_metadata;
        record->metadata_dependencies = input->metadata_dependencies;
        record->metadata_dependency_count = input->metadata_dependency_count;
        record->metadata_content_hash = input->metadata_content_hash;
    }
    return true;
}

static void luna_module_validate_implementations(LunaModuleGraph *graph) {
    for (size_t index = 0U; index < graph->modules.length; index += 1U) {
        const LunaModuleRecord *record =
            luna_vector_at_const(&graph->modules, index);
        if (record->implementation_unit != NULL ||
            record->interface_is_metadata) {
            continue;
        }
        luna_diagnostic_error(
            graph->diagnostics, record->declaration_span,
            "module interface '%.*s' requires a matching implementation unit",
            (int)record->name.length, record->name.data);
    }
}

static const LunaModuleDependency *
luna_module_find_dependency(const LunaModuleRecord *record,
                            uint32_t target_index) {
    for (size_t index = 0U; index < record->dependencies.length; index += 1U) {
        const LunaModuleDependency *dependency =
            luna_vector_at_const(&record->dependencies, index);
        if (dependency->target_index == target_index) {
            return dependency;
        }
    }
    return NULL;
}

static const LunaModuleMetadataDependency *
luna_module_find_metadata_dependency(const LunaModuleRecord *record,
                                     LunaStringView module_name) {
    for (uint32_t index = 0U; index < record->metadata_dependency_count;
         index += 1U) {
        const LunaModuleMetadataDependency *dependency =
            &record->metadata_dependencies[index];
        if (luna_string_view_equal(dependency->module_name, module_name)) {
            return dependency;
        }
    }
    return NULL;
}

static bool luna_module_add_import(LunaModuleGraph *graph,
                                   uint32_t importing_index,
                                   const LunaImport *import,
                                   bool visible_to_interface) {
    LunaModuleRecord *importing =
        luna_vector_at(&graph->modules, (size_t)importing_index);
    const uint32_t target_index =
        luna_module_find_record(graph, import->module_name);
    if (target_index == UINT32_MAX) {
        luna_diagnostic_error(
            graph->diagnostics, import->span,
            "imported module '%.*s' was not supplied to this compilation",
            (int)import->module_name.length, import->module_name.data);
        return false;
    }
    if (target_index == importing_index) {
        luna_diagnostic_error(graph->diagnostics, import->span,
                              "module '%.*s' cannot import itself",
                              (int)importing->name.length,
                              importing->name.data);
        return false;
    }

    const LunaModuleRecord *target =
        luna_vector_at_const(&graph->modules, (size_t)target_index);
    if (target->interface_unit == NULL) {
        luna_diagnostic_error(graph->diagnostics, import->span,
                              "imported module '%.*s' has no interface unit",
                              (int)target->name.length, target->name.data);
        luna_diagnostic_note(graph->diagnostics,
                             target->implementation_unit->module_span,
                             "the implementation-only module is declared here");
        return false;
    }
    if (importing->interface_is_metadata && visible_to_interface) {
        const LunaModuleMetadataDependency *expected =
            luna_module_find_metadata_dependency(importing,
                                                 import->module_name);
        if (expected == NULL) {
            luna_diagnostic_error(
                graph->diagnostics, import->span,
                "compiled metadata for module '%.*s' has no fingerprint for "
                "dependency '%.*s'",
                (int)importing->name.length, importing->name.data,
                (int)import->module_name.length, import->module_name.data);
            return false;
        }
        if (!target->interface_is_metadata) {
            luna_diagnostic_error(
                graph->diagnostics, import->span,
                "compiled module '%.*s' requires dependency '%.*s' as "
                "compiled metadata",
                (int)importing->name.length, importing->name.data,
                (int)target->name.length, target->name.data);
            return false;
        }
        if (expected->content_hash != target->metadata_content_hash) {
            luna_diagnostic_error(
                graph->diagnostics, import->span,
                "compiled module '%.*s' was built against different metadata "
                "for dependency '%.*s'",
                (int)importing->name.length, importing->name.data,
                (int)target->name.length, target->name.data);
            luna_diagnostic_note(
                graph->diagnostics, target->declaration_span,
                "the supplied dependency metadata has an incompatible "
                "content fingerprint");
            return false;
        }
    }

    const LunaModuleDependency *existing =
        luna_module_find_dependency(importing, target_index);
    if (existing != NULL) {
        luna_diagnostic_error(
            graph->diagnostics, import->span,
            "module '%.*s' imports module '%.*s' more than once",
            (int)importing->name.length, importing->name.data,
            (int)target->name.length, target->name.data);
        luna_diagnostic_note(graph->diagnostics, existing->syntax->span,
                             "the first import is here");
        return false;
    }

    const LunaModuleDependency dependency = {
        .target_index = target_index,
        .syntax = import,
    };
    const LunaSemaImport sema_import = {
        .interface_unit = target->interface_unit,
        .span = import->span,
    };
    if (!luna_vector_push(&importing->dependencies, &dependency) ||
        !luna_vector_push(&importing->implementation_imports, &sema_import) ||
        (visible_to_interface &&
         !luna_vector_push(&importing->interface_imports, &sema_import))) {
        luna_module_report_allocation_failure(graph);
        return false;
    }
    return true;
}

static void luna_module_collect_imports(LunaModuleGraph *graph) {
    for (size_t module_index = 0U; module_index < graph->modules.length;
         module_index += 1U) {
        const LunaModuleRecord *record =
            luna_vector_at_const(&graph->modules, module_index);
        const LunaProgram *units[2] = {
            record->interface_unit,
            record->implementation_unit,
        };
        for (uint32_t unit_index = 0U; unit_index < 2U; unit_index += 1U) {
            const LunaProgram *unit = units[unit_index];
            if (unit == NULL) {
                continue;
            }
            for (const LunaImport *import = unit->first_import; import != NULL;
                 import = import->next) {
                (void)luna_module_add_import(graph, (uint32_t)module_index,
                                             import, unit_index == 0U);
            }
        }
    }
}

static const LunaFunction *
luna_module_find_main_candidate(const LunaProgram *implementation_unit) {
    if (implementation_unit == NULL) {
        return NULL;
    }
    for (const LunaFunction *function = implementation_unit->first_function;
         function != NULL; function = function->next) {
        if (luna_string_view_equal_c_string(function->name, "main")) {
            return function;
        }
    }
    return NULL;
}

static uint32_t luna_module_find_executable_root(LunaModuleGraph *graph) {
    uint32_t root_index = UINT32_MAX;
    const LunaFunction *first_main = NULL;
    for (size_t index = 0U; index < graph->modules.length; index += 1U) {
        const LunaModuleRecord *record =
            luna_vector_at_const(&graph->modules, index);
        const LunaFunction *main_function =
            luna_module_find_main_candidate(record->implementation_unit);
        if (main_function == NULL) {
            continue;
        }
        if (root_index == UINT32_MAX) {
            root_index = (uint32_t)index;
            first_main = main_function;
            continue;
        }
        luna_diagnostic_error(
            graph->diagnostics, main_function->span,
            "compilation has more than one module containing 'main'");
        luna_diagnostic_note(graph->diagnostics, first_main->span,
                             "the first executable root candidate is here");
    }

    if (root_index == UINT32_MAX) {
        luna_diagnostic_error_plain(
            graph->diagnostics,
            "module compilation has no implementation unit containing main");
    }
    return root_index;
}

static uint32_t
luna_module_find_separate_root(LunaModuleGraph *graph,
                               LunaStringView requested_module) {
    if (requested_module.data == NULL || requested_module.length == 0U) {
        luna_diagnostic_error_plain(
            graph->diagnostics,
            "separate module compilation requires a root module name");
        return UINT32_MAX;
    }

    const uint32_t root_index =
        luna_module_find_record(graph, requested_module);
    if (root_index == UINT32_MAX) {
        luna_diagnostic_error_plain(
            graph->diagnostics,
            "separate compilation root module '%.*s' was not supplied",
            (int)requested_module.length, requested_module.data);
        return UINT32_MAX;
    }

    const LunaModuleRecord *root =
        luna_vector_at_const(&graph->modules, (size_t)root_index);
    if (root->implementation_unit == NULL) {
        luna_diagnostic_error(
            graph->diagnostics, root->declaration_span,
            "separate compilation root module '%.*s' has no implementation "
            "unit",
            (int)root->name.length, root->name.data);
    }
    if (root->interface_unit == NULL) {
        luna_diagnostic_error(
            graph->diagnostics, root->declaration_span,
            "separate compilation root module '%.*s' requires an interface "
            "unit or compiled metadata",
            (int)root->name.length, root->name.data);
    }

    const LunaFunction *main_function =
        luna_module_find_main_candidate(root->implementation_unit);
    if (main_function != NULL) {
        luna_diagnostic_error(
            graph->diagnostics, main_function->span,
            "separate module compilation cannot define 'main'; compile this "
            "module as an executable");
    }

    for (size_t index = 0U; index < graph->modules.length; index += 1U) {
        if (index == (size_t)root_index) {
            continue;
        }
        const LunaModuleRecord *record =
            luna_vector_at_const(&graph->modules, index);
        if (record->implementation_unit == NULL) {
            continue;
        }
        luna_diagnostic_error(
            graph->diagnostics, record->implementation_unit->module_span,
            "separate compilation of module '%.*s' requires dependency "
            "'%.*s' as compiled metadata, not source implementation",
            (int)root->name.length, root->name.data, (int)record->name.length,
            record->name.data);
    }
    return root_index;
}

static void luna_module_report_cycle(LunaModuleGraph *graph,
                                     uint32_t target_index,
                                     const LunaModuleDependency *closing_edge) {
    size_t cycle_start = 0U;
    while (cycle_start < graph->visit_stack.length) {
        const uint32_t *stack_index =
            luna_vector_at_const(&graph->visit_stack, cycle_start);
        if (*stack_index == target_index) {
            break;
        }
        cycle_start += 1U;
    }

    LunaStringBuilder path;
    luna_string_builder_init(&path);
    bool formatted =
        luna_string_builder_append_c_string(&path, "import cycle detected: ");
    for (size_t index = cycle_start;
         formatted && index < graph->visit_stack.length; index += 1U) {
        const uint32_t *module_index =
            luna_vector_at_const(&graph->visit_stack, index);
        const LunaModuleRecord *record =
            luna_vector_at_const(&graph->modules, (size_t)*module_index);
        formatted = luna_string_builder_append_view(&path, record->name) &&
                    luna_string_builder_append_c_string(&path, " -> ");
    }
    const LunaModuleRecord *target =
        luna_vector_at_const(&graph->modules, (size_t)target_index);
    formatted =
        formatted && luna_string_builder_append_view(&path, target->name);

    if (!formatted) {
        luna_module_report_allocation_failure(graph);
    } else {
        luna_diagnostic_error(graph->diagnostics, closing_edge->syntax->span,
                              "%s", luna_string_builder_data(&path));
        luna_diagnostic_note(graph->diagnostics, target->declaration_span,
                             "the cycle returns to this module");
    }
    luna_string_builder_destroy(&path);
}

static bool luna_module_visit(LunaModuleGraph *graph, uint32_t module_index) {
    LunaModuleRecord *record =
        luna_vector_at(&graph->modules, (size_t)module_index);
    if (record->visit_state == LUNA_MODULE_VISITED) {
        record->reachable = true;
        return true;
    }
    if (record->visit_state == LUNA_MODULE_VISITING) {
        return false;
    }

    record->visit_state = LUNA_MODULE_VISITING;
    record->reachable = true;
    if (!luna_vector_push(&graph->visit_stack, &module_index)) {
        luna_module_report_allocation_failure(graph);
        return false;
    }

    bool success = true;
    for (size_t index = 0U; index < record->dependencies.length; index += 1U) {
        const LunaModuleDependency *dependency =
            luna_vector_at_const(&record->dependencies, index);
        LunaModuleRecord *target =
            luna_vector_at(&graph->modules, (size_t)dependency->target_index);
        if (target->visit_state == LUNA_MODULE_VISITING) {
            luna_module_report_cycle(graph, dependency->target_index,
                                     dependency);
            success = false;
            continue;
        }
        if (!luna_module_visit(graph, dependency->target_index)) {
            success = false;
        }
    }

    graph->visit_stack.length -= 1U;
    record = luna_vector_at(&graph->modules, (size_t)module_index);
    record->visit_state = LUNA_MODULE_VISITED;
    if (!luna_vector_push(&graph->order, &module_index)) {
        luna_module_report_allocation_failure(graph);
        return false;
    }
    return success;
}

static void luna_module_validate_reachability(LunaModuleGraph *graph,
                                              uint32_t root_index) {
    if (root_index == UINT32_MAX) {
        return;
    }
    (void)luna_module_visit(graph, root_index);

    const LunaModuleRecord *root =
        luna_vector_at_const(&graph->modules, (size_t)root_index);
    for (size_t index = 0U; index < graph->modules.length; index += 1U) {
        const LunaModuleRecord *record =
            luna_vector_at_const(&graph->modules, index);
        if (record->reachable) {
            continue;
        }
        luna_diagnostic_error(
            graph->diagnostics, record->declaration_span,
            "supplied module '%.*s' is not reachable from executable root "
            "'%.*s'",
            (int)record->name.length, record->name.data, (int)root->name.length,
            root->name.data);
    }
}

static bool luna_module_lower_order(LunaModuleGraph *graph, uint32_t root_index,
                                    bool is_executable, LunaIrModule *module) {
    LunaVector sema_modules;
    luna_vector_init(&sema_modules, sizeof(LunaSemaModule));

    bool success = true;
    for (size_t index = 0U; index < graph->order.length; index += 1U) {
        const uint32_t *module_index =
            luna_vector_at_const(&graph->order, index);
        const LunaModuleRecord *record =
            luna_vector_at_const(&graph->modules, (size_t)*module_index);
        const LunaSemaModule sema_module = {
            .interface_unit = record->interface_unit,
            .implementation_unit = record->implementation_unit,
            .interface_imports =
                (const LunaSemaImport *)record->interface_imports.data,
            .interface_import_count =
                (uint32_t)record->interface_imports.length,
            .implementation_imports =
                (const LunaSemaImport *)record->implementation_imports.data,
            .implementation_import_count =
                (uint32_t)record->implementation_imports.length,
            .is_precompiled = record->implementation_unit == NULL &&
                              record->interface_is_metadata,
            .is_compilation_root = *module_index == root_index,
            .is_executable_root = is_executable && *module_index == root_index,
            .has_metadata_interface = record->interface_is_metadata,
            .metadata_content_hash = record->metadata_content_hash,
        };
        if (!luna_vector_push(&sema_modules, &sema_module)) {
            luna_module_report_allocation_failure(graph);
            success = false;
            break;
        }
    }

    if (success) {
        success = luna_sema_lower_modules(
            (const LunaSemaModule *)sema_modules.data,
            (uint32_t)sema_modules.length, graph->diagnostics, module);
    }
    luna_vector_destroy(&sema_modules);
    return success;
}

bool luna_module_lower_inputs(const LunaModuleInput *inputs,
                              uint32_t input_count,
                              const LunaModuleOptions *options,
                              LunaDiagnosticEngine *diagnostics,
                              LunaIrModule *module) {
    if (inputs == NULL || input_count == 0U) {
        luna_diagnostic_error_plain(diagnostics,
                                    "module compilation requires source units");
        return false;
    }
    if (options == NULL ||
        (options->compilation_kind != LUNA_MODULE_COMPILE_EXECUTABLE &&
         options->compilation_kind != LUNA_MODULE_COMPILE_SEPARATE)) {
        luna_diagnostic_error_plain(diagnostics,
                                    "module compilation mode is invalid");
        return false;
    }

    LunaModuleGraph graph;
    luna_module_graph_init(&graph, diagnostics);

    for (uint32_t index = 0U; index < input_count; index += 1U) {
        (void)luna_module_add_input(&graph, &inputs[index]);
    }
    luna_module_validate_implementations(&graph);
    luna_module_collect_imports(&graph);
    const bool is_executable =
        options->compilation_kind == LUNA_MODULE_COMPILE_EXECUTABLE;
    const uint32_t root_index =
        is_executable
            ? luna_module_find_executable_root(&graph)
            : luna_module_find_separate_root(&graph, options->root_module_name);
    if (!is_executable && options->require_compiled_root_interface &&
        root_index != UINT32_MAX) {
        const LunaModuleRecord *root =
            luna_vector_at_const(&graph.modules, (size_t)root_index);
        if (!root->interface_is_metadata) {
            luna_diagnostic_error(
                diagnostics, root->declaration_span,
                "separate code generation for module '%.*s' requires its "
                "compiled .lmi interface",
                (int)root->name.length, root->name.data);
        }
    }
    if (luna_diagnostic_error_count(diagnostics) == 0U) {
        luna_module_validate_reachability(&graph, root_index);
    }

    bool success = luna_diagnostic_error_count(diagnostics) == 0U &&
                   !graph.allocation_failed;
    if (success) {
        success =
            luna_module_lower_order(&graph, root_index, is_executable, module);
    }

    success = success && luna_diagnostic_error_count(diagnostics) == 0U &&
              !graph.allocation_failed;
    luna_module_graph_destroy(&graph);
    return success;
}

bool luna_module_lower_programs(const LunaProgram *const *programs,
                                uint32_t program_count,
                                LunaDiagnosticEngine *diagnostics,
                                LunaIrModule *module) {
    if (programs == NULL || program_count == 0U) {
        luna_diagnostic_error_plain(diagnostics,
                                    "module compilation requires source units");
        return false;
    }

    LunaVector inputs;
    luna_vector_init(&inputs, sizeof(LunaModuleInput));
    bool success = true;
    for (uint32_t index = 0U; index < program_count; index += 1U) {
        const LunaModuleInput input = {
            .program = programs[index],
        };
        if (!luna_vector_push(&inputs, &input)) {
            luna_diagnostic_error_plain(
                diagnostics,
                "out of memory while preparing module source units");
            success = false;
            break;
        }
    }

    if (success) {
        const LunaModuleOptions options = {
            .compilation_kind = LUNA_MODULE_COMPILE_EXECUTABLE,
        };
        success = luna_module_lower_inputs((const LunaModuleInput *)inputs.data,
                                           (uint32_t)inputs.length, &options,
                                           diagnostics, module);
    }
    luna_vector_destroy(&inputs);
    return success;
}
