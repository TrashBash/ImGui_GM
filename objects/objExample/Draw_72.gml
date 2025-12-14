gpu_set_ztestenable(true);
gpu_set_zwriteenable(true);


if (!surface_exists(surf))
	surf = surface_create(viewportW, viewportW);
	
if (surface_get_width(surf) != viewportW || surface_get_height(viewportH) != viewportH)
	surface_resize(surf, max(1, viewportW), max(1, viewportH));
	
camera_set_view_mat(cam, viewMatrix)
camera_set_proj_mat(cam, projectionMatrix)
surface_set_target(surf);
draw_clear_alpha(#8FBFBF, 1);
camera_apply(cam);

draw_sprite(spr_grid, 0, 0, 0);

matrix_set(matrix_world, mat);
draw_sprite(sprExample, 0, 0, 0);
matrix_set(matrix_world, matrix_build_identity());

surface_reset_target();


gpu_set_ztestenable(false);
gpu_set_zwriteenable(false);