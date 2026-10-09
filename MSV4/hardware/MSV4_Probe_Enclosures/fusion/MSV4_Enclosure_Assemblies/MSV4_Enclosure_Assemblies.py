"""Fusion 360 importer/exporter for both MSV4 enclosure assemblies.

The solid geometry is generated and checked in the repository first. This
script imports the named STEP assemblies into Fusion 360, adds the controlling
reference parameters, and exports native F3D assemblies plus Fusion-translated
STEP copies. Run it from Utilities > Scripts and Add-Ins.
"""

import adsk.core
import adsk.fusion
import json
import os
import traceback


PARAMETERS_MM = (
    ('pcb_actual_thickness', 1.60, 'Production FR4 thickness; raw Altium STEP is 0.41148 mm'),
    ('pcb_side_clearance', 0.35, 'Clearance per side between the 26 mm PCB head and cavity'),
    ('case_maximum_width', 36.60, 'Local width at the front M2 screw shoulder'),
    ('case_central_width', 33.20, 'Main electronics enclosure width'),
    ('case_neck_width', 20.40, 'Width where the shell meets the 18 mm probe blade'),
    ('case_length', 53.70, 'Electronics-only enclosure length'),
    ('integrated_case_thickness', 10.90, 'Closed integrated-panel central case thickness'),
    ('arm_case_thickness', 9.50, 'Closed arm-mount central case thickness'),
    ('board_to_inner_roof', 4.00, 'Top-side battery and component envelope above PCB'),
    ('bottom_component_clearance', 0.354, 'Clearance below corrected SWD/LED envelope'),
    ('case_nose_thickness', 3.00, 'Tapered shell thickness around the 1.6 mm blade'),
    ('case_floor_thickness', 1.20, 'Closed base floor'),
    ('case_seam_gap', 0.05, 'Nominal base/lid hard-part gap'),
    ('pcb_clamp_interference', 0.10, 'Nominal compression rail interference'),
    ('gasket_groove_width', 0.80, 'Silicone cord or formed-in-place RTV groove'),
    ('gasket_groove_depth', 0.55, 'Silicone cord or formed-in-place RTV groove'),
    ('solar_panel_nominal_length', 36.00, 'User-specified panel length'),
    ('solar_panel_nominal_width', 30.00, 'User-specified panel width'),
    ('solar_panel_pocket_length', 36.40, '0.20 mm clearance per end'),
    ('solar_panel_pocket_width', 30.40, '0.20 mm clearance per side'),
    ('solar_panel_recess_depth', 1.60, 'Bonded front-loading panel recess'),
    ('case_screw_clearance_diameter', 2.40, 'M2 base clearance hole; screws enter from the back'),
    ('case_countersink_top_diameter', 4.20, 'M2 90-degree flat-head countersink under a 0.40 mm recess in the base back'),
    ('case_insert_pocket_diameter', 2.90, 'M2 heat-set insert pilot in the lid seam face'),
    ('case_insert_pocket_depth', 3.20, 'M2 heat-set insert length'),
    ('arm_pivot_center_distance', 34.4607, 'Roof-clearing arm pivot spacing'),
    ('arm_thickness', 1.60, 'Flat-print solar arm thickness'),
)


def _add_parameters(design):
    parameters = design.userParameters
    for name, value, comment in PARAMETERS_MM:
        if parameters.itemByName(name):
            continue
        parameters.add(
            name,
            adsk.core.ValueInput.createByString('{} mm'.format(value)),
            'mm',
            comment,
        )


def _import_and_export(app, source_path, archive_path, fusion_step_path, root_name):
    document = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    design = adsk.fusion.Design.cast(app.activeProduct)
    if not design:
        raise RuntimeError('Fusion did not create a design document.')
    design.designType = adsk.fusion.DesignTypes.ParametricDesignType
    root = design.rootComponent
    _add_parameters(design)

    options = app.importManager.createSTEPImportOptions(source_path)
    options.isViewFit = False
    if not app.importManager.importToTarget(options, root):
        raise RuntimeError('Fusion failed to import {}'.format(source_path))

    manager = design.exportManager
    archive_options = manager.createFusionArchiveExportOptions(archive_path, root)
    if not manager.execute(archive_options):
        raise RuntimeError('Fusion failed to export {}'.format(archive_path))
    step_options = manager.createSTEPExportOptions(fusion_step_path, root)
    if not manager.execute(step_options):
        raise RuntimeError('Fusion failed to export {}'.format(fusion_step_path))

    result = {
        'source': source_path,
        'archive': archive_path,
        'fusion_step': fusion_step_path,
        'root_name': root_name,
        'occurrence_count': root.allOccurrences.count,
        'root_body_count': root.bRepBodies.count,
    }
    document.close(False)
    return result


def _resolve_project_dir(script_dir):
    """Find the enclosure project both in-repo and from Fusion's AddIns copy."""
    candidates = []
    configured_root = os.environ.get('PROJECTTREEBEARD_ROOT')
    if configured_root:
        candidates.append(os.path.join(
            configured_root, 'MSV4', 'hardware', 'MSV4_Probe_Enclosures'))
    candidates.extend((
        os.path.abspath(os.path.join(script_dir, '..', '..')),
        os.path.join(
            os.path.expanduser('~'), 'Desktop', 'ProjectTreebeard', 'MSV4',
            'hardware', 'MSV4_Probe_Enclosures'),
    ))

    required = 'MSV4_integrated_solar_enclosure_assembly.step'
    for candidate in candidates:
        if os.path.isfile(os.path.join(candidate, 'output', 'reference', required)):
            return os.path.abspath(candidate)
    raise RuntimeError(
        'Could not locate MSV4_Probe_Enclosures. Checked: {}'.format(
            ', '.join(os.path.abspath(path) for path in candidates)))


def run(_context):
    app = adsk.core.Application.get()
    ui = app.userInterface
    script_dir = os.path.dirname(os.path.realpath(__file__))
    project_dir = _resolve_project_dir(script_dir)
    output_dir = os.path.join(project_dir, 'output', 'reference')
    fusion_dir = os.path.join(output_dir, 'fusion_exports')
    os.makedirs(fusion_dir, exist_ok=True)

    jobs = (
        (
            'MSV4_integrated_solar_enclosure_assembly.step',
            'MSV4_integrated_solar_enclosure_native.f3d',
            'MSV4_integrated_solar_enclosure_fusion.step',
            'MSV4 Integrated Solar Enclosure',
        ),
        (
            'MSV4_integrated_solar_enclosure_fit_check.step',
            'MSV4_integrated_solar_enclosure_fit_check_native.f3d',
            'MSV4_integrated_solar_enclosure_fit_check_fusion.step',
            'MSV4 Integrated Solar Enclosure - PCB Fit Check',
        ),
        (
            'MSV4_arm_solar_enclosure_assembly.step',
            'MSV4_arm_solar_enclosure_native.f3d',
            'MSV4_arm_solar_enclosure_fusion.step',
            'MSV4 Arm Solar Enclosure',
        ),
        (
            'MSV4_arm_solar_enclosure_fit_check.step',
            'MSV4_arm_solar_enclosure_fit_check_native.f3d',
            'MSV4_arm_solar_enclosure_fit_check_fusion.step',
            'MSV4 Arm Solar Enclosure - PCB Fit Check',
        ),
    )

    results = []
    try:
        for source_name, archive_name, step_name, root_name in jobs:
            source_path = os.path.join(output_dir, source_name)
            if not os.path.isfile(source_path):
                raise RuntimeError('Missing generated STEP assembly: {}'.format(source_path))
            results.append(_import_and_export(
                app,
                source_path,
                os.path.join(fusion_dir, archive_name),
                os.path.join(fusion_dir, step_name),
                root_name,
            ))

        result_path = os.path.join(fusion_dir, 'MSV4_fusion_export_report.json')
        with open(result_path, 'w', encoding='utf-8') as handle:
            json.dump({'passed': True, 'exports': results}, handle, indent=2)
            handle.write('\n')
    except Exception:
        failure = traceback.format_exc()
        result_path = os.path.join(fusion_dir, 'MSV4_fusion_export_report.json')
        with open(result_path, 'w', encoding='utf-8') as handle:
            json.dump({'passed': False, 'error': failure, 'exports': results}, handle, indent=2)
            handle.write('\n')
        ui.messageBox('MSV4 enclosure Fusion export failed:\n{}'.format(failure))


def stop(_context):
    pass
