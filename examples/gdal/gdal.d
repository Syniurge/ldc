/**
 * GDAL D example.
 *
 * Build with:
 *   $ ldc2 gdal.d -L-lgdal
 */

pragma (cppmap, "gdal/gdalwarper.h");
pragma (cppmap, "gdal/cpl_conv.h");    // needed for CPLMalloc

import (C++) *;

void main () {
  GDALDatasetH hSrcDS, hDstDS;

  GDALAllRegister();

  // ne_small.tif and nw_small.tif can be found here if you need them:
  // http://wiki.americaview.org/display/miview/Introduction+to+GDAL
  hSrcDS = GDALOpen( "./ne_small.tif", GDALAccess.GA_ReadOnly );
  hDstDS = GDALOpen( "./nw_small.tif", GDALAccess.GA_Update );

  GDALWarpOptions* psWarpOptions = GDALCreateWarpOptions();

  psWarpOptions.hSrcDS = hSrcDS;
  psWarpOptions.hDstDS = hDstDS;

  psWarpOptions.nBandCount = 1;
  psWarpOptions.panSrcBands = cast(int *) CPLMalloc(int.sizeof * psWarpOptions.nBandCount );

  psWarpOptions.panSrcBands[0] = 1;
  psWarpOptions.panDstBands =
  cast(int *) CPLMalloc(int.sizeof * psWarpOptions.nBandCount );
  psWarpOptions.panDstBands[0] = 1;

  psWarpOptions.pfnProgress = cast(typeof(psWarpOptions.pfnProgress)) &GDALTermProgress;

  // Establish reprojection transformer.
  psWarpOptions.pTransformerArg =
        GDALCreateGenImgProjTransformer( hSrcDS,
                                         GDALGetProjectionRef(hSrcDS),
                                         hDstDS,
                                         GDALGetProjectionRef(hDstDS),
                                         false, 0.0, 1 );

  psWarpOptions.pfnTransformer = cast(typeof(psWarpOptions.pfnTransformer)) &GDALGenImgProjTransform;

  // Initialize and execute the warp operation.
  auto oOperation = new GDALWarpOperation();

  oOperation.Initialize( psWarpOptions );
  oOperation.ChunkAndWarpImage( 0, 0,
                                  GDALGetRasterXSize( hDstDS ),
                                  GDALGetRasterYSize( hDstDS ) );

  GDALDestroyGenImgProjTransformer( psWarpOptions.pTransformerArg );
  GDALDestroyWarpOptions( psWarpOptions );

  GDALClose( hDstDS );
  GDALClose( hSrcDS );

  return;
}

