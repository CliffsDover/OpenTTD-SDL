/*
 *  SDL_uikitopengles.c
 *  iPodSDL
 *
 *  Created by Holmes Futrell on 5/29/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include "SDL_uikitopengles.h"
#include "SDL_uikitopenglview.h"
#include "SDL_uikitappdelegate.h"
#include "SDL_uikitwindow.h"
#include "jump.h"
#include "SDL_sysvideo.h"
#include "SDL_loadso.h"
#include <dlfcn.h>

static int UIKit_GL_Initialize(_THIS);

void *
UIKit_GL_GetProcAddress(_THIS, const char *proc)
{	
	/* Look through all SO's for the proc symbol.  Here's why:
	   -Looking for the path to the OpenGL Library seems not to work in the iPhone Simulator.
	   -We don't know that the path won't change in the future.
	*/
    return SDL_LoadFunction(RTLD_DEFAULT, proc);
}

int UIKit_GL_MakeCurrent(_THIS, SDL_Window * window, SDL_GLContext context)
{
	
	SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
	
	[data->view setCurrentContext];
    return 0;
}

int
UIKit_GL_LoadLibrary(_THIS, const char *path)
{
	/* shouldn't be passing a path into this function */
    if (path != NULL) {
		SDL_SetError("iPhone GL Load Library just here for compatibility");
		return -1;
    }
    return 0;
}


void UIKit_GL_SwapWindow(_THIS, SDL_Window * window)
{

	SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
	
	if (nil == data->view) {
		return;
	}
	[data->view swapBuffers];
	/* since now we've got something to draw
	   make the window visible */
	[data->uiwindow makeKeyAndVisible];

	/* we need to let the event cycle run, or the OS won't update the OpenGL view! */
	SDL_PumpEvents();
	
}

SDL_GLContext UIKit_GL_CreateContext(_THIS, SDL_Window * window)
{
	
	SDL_uikitopenglview *view;

	SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
	
	view = [[SDL_uikitopenglview alloc] initWithFrame: [[UIScreen mainScreen] applicationFrame] \
									retainBacking: _this->gl_config.retained_backing \
									rBits: _this->gl_config.red_size \
									gBits: _this->gl_config.green_size \
									bBits: _this->gl_config.blue_size \
									aBits: _this->gl_config.alpha_size \
									depthBits: _this->gl_config.depth_size];

	view.multipleTouchEnabled = YES;

	data->view = view;

	[data->uiwindow addSubview: view ];
	
	/* Don't worry, the window retained the view */
	[view release];
	
	if ( UIKit_GL_MakeCurrent(_this, window, NULL) < 0 ) {
        UIKit_GL_DeleteContext(_this, NULL);
        return NULL;
    }
		
	return view;
}

void UIKit_GL_DeleteContext(_THIS, SDL_GLContext context)
{
	/* the delegate has retained the view, this will release him */
	SDL_uikitopenglview *view = (SDL_uikitopenglview *)context;
	/* this will also delete it */
	[view removeFromSuperview];
	
	return;
}


