/**
* This file is a part of the Cairo-Dock project
*
* Copyright : (C) see the 'copyright' file.
* E-mail    : see the 'copyright' file.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> 
#include <cairo.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <gdk/gdkx.h>

#ifdef HAVE_GLITZ
#include <gdk/gdkx.h>
#include <glitz-glx.h>
#include <cairo-glitz.h>
#endif
#include <gtk/gtkgl.h>
#include <GL/glu.h>

#include "cairo-dock-draw.h"
#include "cairo-dock-animations.h"
#include "cairo-dock-load.h"
#include "cairo-dock-icons.h"
#include "cairo-dock-applications-manager.h"
#include "cairo-dock-application-facility.h"
#include "cairo-dock-desktop-file-factory.h"
#include "cairo-dock-launcher-manager.h"
#include "cairo-dock-config.h"
#include "cairo-dock-container.h"
#include "cairo-dock-dock-facility.h"
#include "cairo-dock-notifications.h"
#include "cairo-dock-themes-manager.h"
#include "cairo-dock-dialog-manager.h"
#include "cairo-dock-file-manager.h"
#include "cairo-dock-log.h"
#include "cairo-dock-dock-manager.h"
#include "cairo-dock-keybinder.h"
#include "cairo-dock-draw-opengl.h"
#include "cairo-dock-flying-container.h"
#include "cairo-dock-animations.h"
#include "cairo-dock-backends-manager.h"
#include "cairo-dock-internal-accessibility.h"
#include "cairo-dock-internal-system.h"
#include "cairo-dock-internal-taskbar.h"
#include "cairo-dock-internal-icons.h"
#include "cairo-dock-internal-background.h"
#include "cairo-dock-class-manager.h"
#include "cairo-dock-X-manager.h"
#include "cairo-dock-X-utilities.h"
#include "cairo-dock-X-manager.h"
#include "cairo-dock-callbacks.h"

extern gboolean g_bUseOpenGL;
extern CairoDockGLConfig g_openglConfig;

static void _init_opengl (CairoDock *pDock)
{
	if (g_bUseOpenGL)
		cairo_dock_create_redirect_texture_for_dock (pDock);
}

static void _pre_render_opengl (CairoDock *pDock)
{
	if (pDock->iFboId == 0)
		return ;
	
	// on attache la texture au FBO.
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, pDock->iFboId);  // on redirige sur notre FBO.
	glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
		GL_COLOR_ATTACHMENT0_EXT,
		GL_TEXTURE_2D,
		pDock->iRedirectedTexture,
		0);  // attach the texture to FBO color attachment point.
	
	GLenum status = glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT);
	if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		cd_warning ("FBO not ready");
		return;
	}
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

  ///////////////
 // MOVE DOWN //
///////////////

static inline double _compute_y_offset (CairoDock *pDock)
{
	int N = (pDock->bIsHiding ? mySystem.iHideNbSteps : mySystem.iUnhideNbSteps);
	int k = (1 - pDock->fHideOffset) * N;
	double a = pow (1./pDock->iMaxDockHeight, 1./N);  // le dernier step est un ecart d'environ 1 pixel.
	return pDock->iMaxDockHeight * pow (a, k) * (pDock->container.bDirectionUp ? 1 : -1);
}

static void _pre_render_move_down (CairoDock *pDock, cairo_t *pCairoContext)
{
	double dy = _compute_y_offset (pDock);
	if (pDock->container.bIsHorizontal)
		cairo_translate (pCairoContext, 0., dy);
	else
		cairo_translate (pCairoContext, dy, 0.);
}

static void _pre_render_move_down_opengl (CairoDock *pDock)
{
	double dy = _compute_y_offset (pDock);
	if (pDock->container.bIsHorizontal)
		glTranslatef (0., -dy, 0.);
	else
		glTranslatef (dy, 0., 0.);
}

  //////////////
 // FADE OUT //
//////////////

static void _init_fade_out (CairoDock *pDock)
{
	if (g_bUseOpenGL && ! g_openglConfig.bStencilBufferAvailable)
		cairo_dock_create_redirect_texture_for_dock (pDock);
}

static void _pre_render_fade_out_opengl (CairoDock *pDock)
{
	if (! g_openglConfig.bStencilBufferAvailable && pDock->iFboId != 0)  // pas de glAccum.
	{
		_pre_render_opengl (pDock);
	}
}

static void _post_render_fade_out (CairoDock *pDock, cairo_t *pCairoContext)
{
	double fAlpha = 1 - pDock->fHideOffset;
	cairo_rectangle (pCairoContext,
		0,
		0,
		pDock->container.bIsHorizontal ? pDock->container.iWidth : pDock->container.iHeight, pDock->container.bIsHorizontal ? pDock->container.iHeight : pDock->container.iWidth);
	cairo_set_line_width (pCairoContext, 0);
	cairo_set_operator (pCairoContext, CAIRO_OPERATOR_DEST_OUT);
	cairo_set_source_rgba (pCairoContext, 0.0, 0.0, 0.0, 1. - fAlpha);
	cairo_fill (pCairoContext);
}

static void _post_render_fade_out_opengl (CairoDock *pDock)
{
	double fAlpha = 1 - pDock->fHideOffset;
	if (g_openglConfig.bStencilBufferAvailable)
	{
		glAccum (GL_LOAD, fAlpha*fAlpha);
		glAccum (GL_RETURN, 1.0);
	}
	else if (pDock->iFboId != 0)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);  // switch back to window-system-provided framebuffer
		glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
			GL_COLOR_ATTACHMENT0_EXT,
			GL_TEXTURE_2D,
			0,
			0);  // on detache la texture (precaution).
		// dessin dans notre fenetre.
		_cairo_dock_enable_texture ();
		_cairo_dock_set_blend_source ();
		_cairo_dock_set_blend_alpha ();
		
		int iWidth, iHeight;  // taille de la texture
		if (pDock->container.bIsHorizontal)
		{
			iWidth = pDock->container.iWidth;
			iHeight = pDock->container.iHeight;
		}
		else
		{
			iWidth = pDock->container.iHeight;
			iHeight = pDock->container.iWidth;
		}
		
		glPushMatrix ();
		glLoadIdentity ();
		glTranslatef (iWidth/2, iHeight/2, - iHeight/2);
		glScalef (1., -1., 1.);
		_cairo_dock_apply_texture_at_size_with_alpha (pDock->iRedirectedTexture, iWidth, iHeight, fAlpha);
		glPopMatrix ();
		
		_cairo_dock_disable_texture ();
	}
}

  //////////////////////
 // SEMI TRANSPARENT //
//////////////////////

static void _post_render_semi_transparent (CairoDock *pDock, cairo_t *pCairoContext)
{
	double fAlpha = .75*pDock->fHideOffset;
	cairo_rectangle (pCairoContext,
		0,
		0,
		pDock->container.bIsHorizontal ? pDock->container.iWidth : pDock->container.iHeight, pDock->container.bIsHorizontal ? pDock->container.iHeight : pDock->container.iWidth);
	cairo_set_line_width (pCairoContext, 0);
	cairo_set_operator (pCairoContext, CAIRO_OPERATOR_DEST_OUT);
	cairo_set_source_rgba (pCairoContext, 0.0, 0.0, 0.0, fAlpha);
	cairo_fill (pCairoContext);
}

static void _post_render_semi_transparent_opengl (CairoDock *pDock)
{
	double fAlpha = 1 - .75*pDock->fHideOffset;
	if (g_openglConfig.bStencilBufferAvailable)
	{
		glAccum (GL_LOAD, fAlpha);
		glAccum (GL_RETURN, 1.0);
	}
	else if (pDock->iFboId != 0)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);  // switch back to window-system-provided framebuffer
		glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
			GL_COLOR_ATTACHMENT0_EXT,
			GL_TEXTURE_2D,
			0,
			0);  // on detache la texture (precaution).
		// dessin dans notre fenetre.
		_cairo_dock_enable_texture ();
		_cairo_dock_set_blend_source ();
		_cairo_dock_set_blend_alpha ();
		
		int iWidth, iHeight;  // taille de la texture
		if (pDock->container.bIsHorizontal)
		{
			iWidth = pDock->container.iWidth;
			iHeight = pDock->container.iHeight;
		}
		else
		{
			iWidth = pDock->container.iHeight;
			iHeight = pDock->container.iWidth;
		}
		
		glPushMatrix ();
		glLoadIdentity ();
		glTranslatef (iWidth/2, iHeight/2, - iHeight/2);
		glScalef (1., -1., 1.);
		_cairo_dock_apply_texture_at_size_with_alpha (pDock->iRedirectedTexture, iWidth, iHeight, fAlpha);
		glPopMatrix ();
		
		_cairo_dock_disable_texture ();
	}
}

  //////////////
 // ZOOM OUT //
//////////////

static inline double _compute_zoom (CairoDock *pDock)
{
	int N = (pDock->bIsHiding ? mySystem.iHideNbSteps : mySystem.iUnhideNbSteps);
	int k = pDock->fHideOffset * N;
	double a = pow (1./pDock->iMaxDockHeight, 1./N);  // le premier step est un ecart d'environ 1 pixels.
	return 1 - pow (a, N - k);
}

static void _pre_render_zoom (CairoDock *pDock, cairo_t *pCairoContext)
{
	double z = _compute_zoom (pDock);
	int iWidth, iHeight;
	if (pDock->container.bIsHorizontal)
	{
		iWidth = pDock->container.iWidth;
		iHeight = pDock->container.iHeight;
	}
	else
	{
		iWidth = pDock->container.iHeight;
		iHeight = pDock->container.iWidth;
	}
	cairo_translate (pCairoContext, iWidth/2, iHeight);
	cairo_scale (pCairoContext, z, z);
	cairo_translate (pCairoContext, -iWidth/2, -iHeight);
}

static void _post_render_zoom_opengl (CairoDock *pDock)
{
	if (pDock->iFboId == 0)
		return ;
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);  // switch back to window-system-provided framebuffer
	glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
		GL_COLOR_ATTACHMENT0_EXT,
		GL_TEXTURE_2D,
		0,
		0);  // on detache la texture (precaution).
	// dessin dans notre fenetre.
	_cairo_dock_enable_texture ();
	_cairo_dock_set_blend_source ();
	
	int iWidth, iHeight;  // taille de la texture
	if (pDock->container.bIsHorizontal)
	{
		iWidth = pDock->container.iWidth;
		iHeight = pDock->container.iHeight;
	}
	else
	{
		iWidth = pDock->container.iHeight;
		iHeight = pDock->container.iWidth;
	}
	
	glPushMatrix ();
	glLoadIdentity ();
	glTranslatef (iWidth/2, 0., - iHeight/2);
	
	double z = _compute_zoom (pDock);
	glScalef (z, z, 1.);
	glTranslatef (0., iHeight/2, - iHeight/2);
	
	glScalef (1., -1., 1.);
	_cairo_dock_apply_texture_at_size_with_alpha (pDock->iRedirectedTexture, iWidth, iHeight, 1.);
	
	glPopMatrix ();
	
	_cairo_dock_disable_texture ();
}

  /////////////
 // SUCK UP //
/////////////

#define NB_POINTS 11  // 5 carres de part et d'autre.
static void _post_render_suck_up_opengl (CairoDock *pDock)
{
	if (pDock->iFboId == 0)
		return ;
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);  // switch back to window-system-provided framebuffer
	glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
		GL_COLOR_ATTACHMENT0_EXT,
		GL_TEXTURE_2D,
		0,
		0);  // on detache la texture (precaution).
	// dessin dans notre fenetre.
	_cairo_dock_enable_texture ();
	_cairo_dock_set_blend_alpha ();
	_cairo_dock_set_alpha (1.);
	
	int iWidth, iHeight;  // taille de la texture
	if (pDock->container.bIsHorizontal)
	{
		iWidth = pDock->container.iWidth;
		iHeight = pDock->container.iHeight;
	}
	else
	{
		iWidth = pDock->container.iHeight;
		iHeight = pDock->container.iWidth;
	}
	
	glPushMatrix ();
	glLoadIdentity ();
	glTranslatef (iWidth/2, 0., 0.);
	
	GLfloat coords[NB_POINTS*1*8];
	GLfloat vertices[NB_POINTS*1*8];
	int i, j, n=0;
	double x, x_, t = pDock->fHideOffset;
	for (i = 0; i < NB_POINTS; i ++)
	{
		for (j = 0; j < 2-1; j ++)
		{
			x = (double)i/NB_POINTS;
			x_ = (double)(i+1)/NB_POINTS;
			coords[8*n+0] = x;  // haut gauche
			coords[8*n+1] = 1.;
			coords[8*n+2] = x_;  // haut droit
			coords[8*n+3] = 1.;
			coords[8*n+4] = x_;  // bas droit
			coords[8*n+5] = t;
			coords[8*n+6] = x;  // bas gauche
			coords[8*n+7] = t;
			
			vertices[8*n+0] = pow (fabs (x-.5), 1+t/3) * (x<.5?-1:1);  // on etire un peu en haut
			vertices[8*n+1] = 1 - t;
			vertices[8*n+2] = pow (fabs (x_-.5), 1+t/3) * (x_<.5?-1:1);
			vertices[8*n+3] = 1 - t;
			vertices[8*n+4] = pow (fabs (x_-.5), 1+5*t) * (x_<.5?-1:1);  // et beaucoup en bas.
			vertices[8*n+5] = 0.;
			vertices[8*n+6] = pow (fabs (x-.5), 1+5*t) * (x<.5?-1:1);
			vertices[8*n+7] = 0.;
			
			n ++;
		}
	}
	
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glEnableClientState (GL_VERTEX_ARRAY);
	
	glScalef (iWidth, iHeight, 1.);
	glBindTexture (GL_TEXTURE_2D, pDock->iRedirectedTexture);
	glTexCoordPointer (2, GL_FLOAT, 2 * sizeof(GLfloat), coords);
	glVertexPointer (2, GL_FLOAT, 2 * sizeof(GLfloat), vertices);
	glDrawArrays (GL_QUADS, 0, n * 4);
	
	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glDisableClientState (GL_VERTEX_ARRAY);
	
	glPopMatrix ();
	
	_cairo_dock_disable_texture ();
}

  /////////////
 // FOLDING //
/////////////

static void _pre_render_folding (CairoDock *pDock, cairo_t *pCairoContext)
{
	double z = (1-pDock->fHideOffset) * (1-pDock->fHideOffset);
	int iWidth, iHeight;
	if (pDock->container.bIsHorizontal)
	{
		iWidth = pDock->container.iWidth;
		iHeight = pDock->container.iHeight;
	}
	else
	{
		iWidth = pDock->container.iHeight;
		iHeight = pDock->container.iWidth;
	}
	cairo_translate (pCairoContext, iWidth/2, iHeight);
	cairo_scale (pCairoContext, z, 1.);
	cairo_translate (pCairoContext, -iWidth/2, -iHeight);
}

static void _post_render_folding_opengl (CairoDock *pDock)
{
	if (pDock->iFboId == 0)
		return ;
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);  // switch back to window-system-provided framebuffer
	glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
		GL_COLOR_ATTACHMENT0_EXT,
		GL_TEXTURE_2D,
		0,
		0);  // on detache la texture (precaution).
	// dessin dans notre fenetre.
	_cairo_dock_enable_texture ();
	_cairo_dock_set_blend_alpha ();
	_cairo_dock_set_alpha (1.);
	
	int iWidth, iHeight;  // taille de la texture
	if (pDock->container.bIsHorizontal)
	{
		iWidth = pDock->container.iWidth;
		iHeight = pDock->container.iHeight;
	}
	else
	{
		iWidth = pDock->container.iHeight;
		iHeight = pDock->container.iWidth;
	}
	
	cairo_dock_set_perspective_view (CAIRO_CONTAINER (pDock));
	glPushMatrix ();
	//glLoadIdentity ();
	//glTranslatef (iWidth/2, 0., 0.);
	glTranslatef (0., -iHeight/2, 0.);
	
	GLfloat coords[NB_POINTS*1*8];
	GLfloat vertices[NB_POINTS*1*12];
	int i, j, n=0;
	double x, x_, t = pDock->fHideOffset;
	t = t* (t);
	for (i = 0; i < NB_POINTS; i ++)
	{
		x = (double)i/NB_POINTS;
		x_ = (double)(i+1)/NB_POINTS;
		coords[8*n+0] = x;  // haut gauche
		coords[8*n+1] = 1.;
		coords[8*n+2] = x_;  // haut droit
		coords[8*n+3] = 1.;
		coords[8*n+4] = x_;  // bas droit
		coords[8*n+5] = 0.;
		coords[8*n+6] = x;  // bas gauche
		coords[8*n+7] = 0.;
		
		vertices[12*n+0] = (x-.5) * (1-t);
		vertices[12*n+1] = 1.;
		vertices[12*n+2] = (i&1 ? -t : 0.);
		
		vertices[12*n+3] = (x_-.5) * (1-t);
		vertices[12*n+4] = 1.;
		vertices[12*n+5] = (i&1 ? 0. : -t);
		
		vertices[12*n+6] = (x_-.5) * (1-t);
		vertices[12*n+7] = 0.;
		vertices[12*n+8] = (i&1 ? 0. : -t);
		
		vertices[12*n+9] = (x-.5) * (1-t);
		vertices[12*n+10] = 0.;
		vertices[12*n+11] = (i&1 ? -t : 0.);
		
		n ++;
	}
	
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glEnableClientState (GL_VERTEX_ARRAY);
	
	glScalef (iWidth, iHeight, iHeight/6);
	glBindTexture (GL_TEXTURE_2D, pDock->iRedirectedTexture);
	glTexCoordPointer (2, GL_FLOAT, 2 * sizeof(GLfloat), coords);
	glVertexPointer (3, GL_FLOAT, 3 * sizeof(GLfloat), vertices);
	glDrawArrays (GL_QUADS, 0, n * 4);
	
	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glDisableClientState (GL_VERTEX_ARRAY);
	
	cairo_dock_set_ortho_view (CAIRO_CONTAINER (pDock));
	glPopMatrix ();
	
	_cairo_dock_disable_texture ();
}

void cairo_dock_register_hiding_effects (void)
{
	CairoDockHidingEffect *p;
	p = g_new0 (CairoDockHidingEffect, 1);
	p->cDisplayedName = _("Move down");
	p->pre_render = _pre_render_move_down;
	p->pre_render_opengl = _pre_render_move_down_opengl;
	cairo_dock_register_hiding_effect ("Move down", p);
	
	p = g_new0 (CairoDockHidingEffect, 1);
	p->cDisplayedName = _("Fade out");
	p->init = _init_fade_out;
	p->pre_render_opengl = _pre_render_fade_out_opengl;
	p->post_render = _post_render_fade_out;
	p->post_render_opengl = _post_render_fade_out_opengl;
	cairo_dock_register_hiding_effect ("Fade out", p);
	
	p = g_new0 (CairoDockHidingEffect, 1);
	p->cDisplayedName = _("Semi transparent");
	p->init = _init_fade_out;
	p->pre_render_opengl = _pre_render_fade_out_opengl;
	p->post_render = _post_render_semi_transparent;
	p->post_render_opengl = _post_render_semi_transparent_opengl;
	p->bCanDisplayHiddenDock = TRUE;
	cairo_dock_register_hiding_effect ("Semi transparent", p);
	
	p = g_new0 (CairoDockHidingEffect, 1);
	p->cDisplayedName = _("Zoom out");
	p->init = _init_opengl;
	p->pre_render = _pre_render_zoom;
	p->pre_render_opengl = _pre_render_opengl;
	p->post_render_opengl = _post_render_zoom_opengl;
	cairo_dock_register_hiding_effect ("Zoom out", p);
	
	p = g_new0 (CairoDockHidingEffect, 1);
	p->cDisplayedName = _("Suck up");
	p->init = _init_opengl;
	p->pre_render = _pre_render_move_down;
	p->pre_render_opengl = _pre_render_opengl;
	p->post_render_opengl = _post_render_suck_up_opengl;
	cairo_dock_register_hiding_effect ("Suck up", p);
	
	p = g_new0 (CairoDockHidingEffect, 1);
	p->cDisplayedName = _("Folding");
	p->init = _init_opengl;
	p->pre_render = _pre_render_folding;
	p->pre_render_opengl = _pre_render_opengl;
	p->post_render_opengl = _post_render_folding_opengl;
	cairo_dock_register_hiding_effect ("Folding", p);
}