// Gestion des sprites.
// Code: 17o2!! (Cl�ment CORDE)

#include "includes.h"
#include "sprites_inc.h"

#define	SPRGRAB_DISPLAY_INFO	0		// Mettre � 0 pour ne pas afficher les infos de capture / 1 pour affichage.
#define	SPRPAL_SUB_ON	1				// Mettre � 1 pour sauver des palettes par x couleurs au lieu de 256.
//#define	DEBUG_INFO	1	// Commenter pour supprimer.

#if	SPRPAL_SUB_ON == 1
#define	SPR_PAL_SZ	1	//8	//16
#else
#define	SPR_PAL_SZ	256
#endif

//#ifdef __LINUX__
#if defined (__LINUX__) || defined (__APPLE__)
// stricmp n'existe pas en Linux : C'est strcasecmp � la place, dans strings.h.
int stricmp(char *pStr1, char *pStr2)
{
	return (strcasecmp(pStr1, pStr2));
}
#endif


// Pour capture des sprites.
#define	SPRDEF_ALLOC_UNIT	256
struct SSprite	*gpSprDef;	// D�finitions des sprites.
u32	gnSprNbSprDefMax;	// Nb de sprites max capturables jusqu'au prochain realloc.
u32	gnSprNbSprites;		// Nb de sprites captur�s.

#define	SPRBUF_ALLOC_UNIT	(1024 * 1024)
u8	*gpSprBuf;		// Datas des sprites.
u32	gnSprBufSz;		// Taille du buffer de data.
u32	gnSprBufAllocSz;	// Taille du buffer de data allou�e pour ne pas faire de r�allocs sans arr�t.

u16	*gpSprRemapPalettes;	// Palettes de remappage des sprites bout � bout. 1 u16 par couleur au format �cran (c�d pas RGB), x u16 par pal (voir SPRPAL_SUB_ON et SPR_PAL_SZ).
u32	gnSprRemapPalettesNb;	// Nb de palettes.
u8	*gpSprPal3Bytes;		// Les couleurs sur 3 bytes.

u16	*gpSprFlipBuf;		// Buffer pour cr�er les images flipp�es.

extern u8	*gpRotBuf;	// Buffer pour rendu de la rotation. Sz = ROT2D_BUF_Width * ROT2D_BUF_Height. Pas dans le .H car n'a pas a �tre connu d'autre chose que le moteur de sprites.


// Pour tri des sprites � chaque frame.
struct SSprStockage
{
	u32 nSprNo;
	s32 nPosX, nPosY;
	u16 nPrio;
	// Pour Roto/Zoom :
	u16	nZoomX;				// Rot : nAngle + nZoomX / Zoom slt : nZoomX + nZoomY.
	union
	{
		u16	nZoomY;
		u8	nAngle;
	};
	void	*pFct;			// Ptr sur fct de pr�-rendu puis de rendu (m�j par fct de pr�-rendu) de zoom ou de rotozoom. NULL pour un sprite normal.
};
#define	SPR_STO_MAX	512
struct SSprStockage	gpSprSto[SPR_STO_MAX];
struct SSprStockage	*gpSprSort[SPR_STO_MAX];	// Pour tri.
u32	gnSprSto;			// Nb de sprites stock�s pour affichage.


// Initialisation du moteur (1 fois !).
void SprInitEngine(void)
{
//printf("SSprStockage sz = %d\n", sizeof(struct SSprStockage));

	gpSprDef = NULL;		// D�finitions des sprites.
	gnSprNbSprDefMax = 0;	// Nb de sprites max capturables jusqu'au prochain realloc.
	gnSprNbSprites = 0;		// Nb de sprites captur�s.

	gpSprBuf = NULL;	// Datas des sprites.
	gnSprBufSz = 0;		// Taille du buffer de data.
	gnSprBufAllocSz = 0;		// Taille du buffer de data allou�e pour ne pas faire de r�allocs sans arr�t.

	gnSprSto = 0;		// Nb de sprites stock�s pour affichage.

	gpSprRemapPalettes = NULL;	// Palettes de remappage.
	gnSprRemapPalettesNb = 0;	// Nb de palettes.
	gpSprPal3Bytes = NULL;		// Les couleurs sur 3 bytes.

	gpSprFlipBuf = NULL;	// Buffer pour cr�er les images flipp�es.
	gpRotBuf = NULL;		// Buffer pour rendu de la rotation.

	#if CACHE_ON == 1
	CacheClear();		// RAZ cache.
	#endif

}

// Secure fopen.
FILE * sec_fopen(char *pFilename, char *pMode)
{
	FILE	*fPt;

	fPt = fopen(pFilename, pMode);
	if (fPt == NULL)
	{
		fprintf(stderr, "SprBin_sub_fopen(): Error opening file '%s'.\n", pFilename);
		exit (1);
	}
	return (fPt);
}

// Calcul d'un checksum pour les fichiers binaires.
u32 SprChecksum(u8 *pData, u32 nDataSz)
{
	u32	i;
	u32	nRem;
	u32	nSum;

	nRem = nDataSz & 3;
	nDataSz >>= 2;
	nSum = 0;
	// Quads.
	for (i = 0; i < nDataSz; i++)
	{
		nSum += *(u32 *)pData;
		pData += 4;
	}
	// Octets restants.
	for (i = 0; i < nRem; i++)
	{
		nSum += *pData;
		pData++;
	}
	return (nSum);
}

#if SPR_SAVE == 1		// Lecture des fichiers graphiques et sauvegarde des datas.
void SprBinSave_sub(char *pFilename, u8 *pSrc, u32 nSavSz)
{
	FILE	*fPt;
	u32	nSz;
	u32	nChecksum;

	nChecksum = SprChecksum(pSrc, nSavSz);

	fPt = sec_fopen(pFilename, "wb");
	nSz = fwrite(pSrc, 1, nSavSz, fPt);
	nSz += fwrite(&nChecksum, 1, sizeof(u32), fPt);
	fclose(fPt);
	if (nSz != nSavSz + sizeof(u32))
	{
		fprintf(stderr, "SprBinariesSave(): %s: Error, wrong file size written (%d bytes written, %d bytes expected).\n", pFilename, (int)nSz, (int)nSavSz);
		exit (1);
	}
printf("File '%s', Checksum: %x\n", pFilename, nChecksum);
}

// Sauvegarde des sprites en fichiers binaires.
void SprBinariesSave(void)
{
	// D�finitions.
#if defined (CPU64)
	SprBinSave_sub("gfx/sprdef64.bin", (u8 *)gpSprDef, sizeof(struct SSprite) * gnSprNbSprites);
#else
	SprBinSave_sub("gfx/sprdef.bin", (u8 *)gpSprDef, sizeof(struct SSprite) * gnSprNbSprites);
#endif
	// Les graphs.
	SprBinSave_sub("gfx/sprbuf.bin", gpSprBuf, gnSprBufSz);
	// Les palettes.
	SprBinSave_sub("gfx/sprpal.bin", gpSprPal3Bytes, gnSprRemapPalettesNb * 3);

}
#else
u8 * SprBinLoad_sub(char *pFilename, u32 *pnSz)
{
	FILE	*fPt;
	u32	nSz1, nSz2, nSz3;
	u8	*pBuf;
	u32	nChkRead, nChkCalc;

	// D�finitions.
	fPt = sec_fopen(pFilename, "rb");
	fseek(fPt, 0, SEEK_END);
	nSz1 = ftell(fPt);
	// Size <= taille du checksum ?
	if (nSz1 <= sizeof(u32))
	{
		fprintf(stderr, "SprBinariesLoad(): %s: Wrong file size (%d bytes).\n", pFilename, (int)nSz1);
		fclose(fPt);
		exit (1);
	}
	nSz1 -= sizeof(u32);
	rewind(fPt);
	// malloc.
	if ((pBuf = (u8 *)malloc(nSz1)) == NULL)
	{
		fprintf(stderr, "SprBinariesLoad(): %s: Error allocating memory.\n", pFilename);
		fclose(fPt);
		exit (1);
	}
	// Lecture.
	nSz2 = fread(pBuf, 1, nSz1, fPt);
	nSz3 = fread(&nChkRead, 1, sizeof(u32), fPt);
	fclose(fPt);
	if (nSz1 != nSz2)
	{
		fprintf(stderr, "SprBinariesLoad(): %s: Error, wrong file size loaded (%d bytes loaded, %d bytes expected).\n", pFilename, (int)nSz2, (int)nSz1);
		exit (1);
	}
	if (nSz3 != sizeof(u32))
	{
		fprintf(stderr, "SprBinariesLoad(): %s: Error while loading checksum, wrong size.\n", pFilename);
		exit (1);
	}
	// Checksum ok ?
	nChkCalc = SprChecksum(pBuf, nSz1);
	if (nChkCalc != nChkRead)
	{
		fprintf(stderr, "SprBinariesLoad(): %s: Checksum error (calc: %x, read: %x).\n", pFilename, (int)nChkCalc, (int)nChkRead);
		exit (1);
	}

	*pnSz = nSz1;
	return (pBuf);
}
// Lecture des fichiers binaires des sprites.
void SprBinariesLoad(void)
{
	u32	nSz;

	// D�finitions.
#if defined (CPU64)
	gpSprDef = (struct SSprite *)SprBinLoad_sub("gfx/sprdef64.bin", &nSz);
#else
	gpSprDef = (struct SSprite *)SprBinLoad_sub("gfx/sprdef.bin", &nSz);
#endif
	gnSprNbSprites = nSz / sizeof(struct SSprite);
  #ifdef DEBUG_INFO
printf("SprLoad: gnSprNbSprites=%d\n", gnSprNbSprites);
  #endif
	// Les graphs.
	gpSprBuf = SprBinLoad_sub("gfx/sprbuf.bin", &nSz);
	// Les palettes.
	gpSprPal3Bytes = SprBinLoad_sub("gfx/sprpal.bin", &nSz);
	gnSprRemapPalettesNb = nSz / 3;
  #ifdef DEBUG_INFO
printf("SprLoad: gnSprRemapPalettesNb=%d\n", gnSprRemapPalettesNb);
  #endif

}
#endif

// Termine la capture, mise en place des pointeurs (1 fois !).
void SprEndCapture(void)
{
	u32	i;
	u32	nLgMax, nHtMax;		// Pour allocation du buffer de flips.

	nLgMax = 0;
	nHtMax = 0;
	for (i = 0; i < gnSprNbSprites; i++)
	{
		// Replace le pointeur (offset sauv� dans l'union � la lecture).
		gpSprDef[i].pGfx8 = gpSprBuf + gpSprDef[i].nGfx8Offset;
		// Recherche lg et ht max.
		if (gpSprDef[i].nLg > nLgMax) nLgMax = gpSprDef[i].nLg;
		if (gpSprDef[i].nHt > nHtMax) nHtMax = gpSprDef[i].nHt;
	}
#ifdef DEBUG_INFO
printf("Spr biggest sz: lg=%d ht=%d\n", (int)nLgMax, (int)nHtMax);
#endif
	// Pour la gestion des Roto/Zooms, on pr�voit au minimum la taille du buffer de rendu de rotations.
	if (nLgMax < ROT2D_BUF_Width) nLgMax = ROT2D_BUF_Width;
	if (nHtMax < ROT2D_BUF_Height) nHtMax = ROT2D_BUF_Height;

	// Allocation d'un buffer pour "depacker" les sprites de 8 � 16 bits, + g�n�ration du masque.
	if (nLgMax == 0 || nHtMax == 0)
	{
		fprintf(stderr, "SprEndCapture(): Zero max with/height found. Aborted.\n");
		exit(1);
	}
	if ((gpSprFlipBuf = (u16 *)malloc(nLgMax * nHtMax * sizeof(u16) * 2)) == NULL)
	{
		fprintf(stderr, "SprEndCapture(): malloc failed (gpSprFlipBuf).\n");
		exit(1);
	}
	if ((gpRotBuf = (u8 *)malloc(ROT2D_BUF_Width * ROT2D_BUF_Height * sizeof(u8))) == NULL)
	{
		fprintf(stderr, "SprEndCapture(): malloc failed (gpRotBuf).\n");
		exit(1);
	}

	// Alloc m�moire palettes de remappage.
	if ((gpSprRemapPalettes = (u16 *)malloc(gnSprRemapPalettesNb * SPR_PAL_SZ * sizeof(u16))) == NULL)
	{
		printf("SprEndCapture(): malloc failed (gpSprRemapPalettes).\n");
		exit(1);
	}
	SprPaletteConversion();		// Conversion couleurs RGB > u16.

#ifdef DEBUG_INFO
printf("Spr biggest sz (2): lg=%d ht=%d\n", (int)nLgMax, (int)nHtMax);
printf("Total mem: Allocated = %d / used = %d\n", (int)gnSprBufAllocSz, (int)gnSprBufSz);	// debug
printf("gnSprRemapPalettesNb=%d\n", gnSprRemapPalettesNb);
#endif

}

// Nettoyage (1 fois !).
void SprRelease(void)
{
	free(gpSprBuf);		// On lib�re les datas.
	free(gpSprDef);		// On lib�re les d�finitions.
	free(gpSprRemapPalettes);	// On lib�re les palettes de remappage.
	free(gpSprPal3Bytes);		// Les couleurs sur 3 bytes.
	free(gpSprFlipBuf);	// Buffer pour cr�er les images flipp�es.
	free(gpRotBuf);		// Buffer pour g�n�ration des images roto/zoom�es.

}


#if SPR_SAVE == 1
// Realloc des definitions de sprites quand toutes les struct dispo ont �t� remplies.
void SprDefRealloc(void)
{
	gnSprNbSprDefMax += SPRDEF_ALLOC_UNIT;
	printf("SprDefRealloc(): New size=%d.\n", (int)gnSprNbSprDefMax);

	gpSprDef = (struct SSprite *)realloc(gpSprDef, gnSprNbSprDefMax * sizeof(struct SSprite));
	if (gpSprDef == NULL)
	{
		printf("SprDefRealloc: realloc failed.\n");
		SprRelease();
		exit(1);
	}
	memset((u8 *)gpSprDef + ((gnSprNbSprDefMax - SPRDEF_ALLOC_UNIT) * sizeof(struct SSprite)), 0, SPRDEF_ALLOC_UNIT * sizeof(struct SSprite));
}


// Realloc des datas des sprites.
void SprBufRealloc(void)
{
	gnSprBufAllocSz += SPRBUF_ALLOC_UNIT;
	printf("SprBufRealloc(): New size=%d.\n", (int)gnSprBufAllocSz);

//	gpSprBuf = (u16 *)realloc(gpSprBuf, gnSprBufAllocSz);
	gpSprBuf = (u8 *)realloc(gpSprBuf, gnSprBufAllocSz);
	if (gpSprBuf == NULL)
	{
		printf("SprBufRealloc(): realloc failed.\n");
		SprRelease();
		exit(1);
	}
}
#endif

// Renvoie un ptr sur une palette de remappage.
u16 * SprRemapPalGet(u32 nPalNo)
{
	return (gpSprRemapPalettes + (nPalNo * SPR_PAL_SZ));
}

/*
// Alloue de la m�moire pour une nouvelle palette de remappage.
// In: nNbPalToAdd = Nombre de palettes de x couleurs suppl�mentaires � allouer.
u16 * SprRemapPalAlloc(u32 nNbPalToAdd)
{
	// Alloc m�moire palettes de remappage.
	if ((gpSprRemapPalettes = (u16 *)realloc(gpSprRemapPalettes, (gnSprRemapPalettesNb + nNbPalToAdd) * SPR_PAL_SZ * sizeof(u16))) == NULL)
	{
		printf("SprRemapPalAlloc(): realloc failed.\n");
		exit(1);
	}
	gnSprRemapPalettesNb += nNbPalToAdd;
//printf("Palettes sz=%d bytes.\n", (int)(gnSprRemapPalettesNb * SPR_PAL_SZ * sizeof(u16)) );

	return (SprRemapPalGet(gnSprRemapPalettesNb - nNbPalToAdd));
}
*/

// Alloue de la m�moire pour les palettes des sprites.
// In: nNbPalToAdd = Nombre de palettes de x couleurs suppl�mentaires � allouer.
u8 * SprPal3BytesAlloc(u32 nNbPalToAdd)
{
	// Alloc m�moire pour palettes RGB.
	if ((gpSprPal3Bytes = (u8 *)realloc(gpSprPal3Bytes, (gnSprRemapPalettesNb + nNbPalToAdd) * SPR_PAL_SZ * 3)) == NULL)
	{
		printf("SprPal3BytesAlloc(): realloc failed.\n");
		exit(1);
	}
	gnSprRemapPalettesNb += nNbPalToAdd;

	return (gpSprPal3Bytes + ((gnSprRemapPalettesNb - nNbPalToAdd) * SPR_PAL_SZ * 3));
}

// Convertit la palette RGB 3 bytes en couleurs SDL 16 bits.
// Conversion s�par�e pour pouvoir au cas ou la refaire quand changement de mode vid�o.
void SprPaletteConversion(void)
{
	u32	i;

	for (i = 0; i < gnSprRemapPalettesNb; i++)
		*(gpSprRemapPalettes + i) = SDL_MapRGB(gVar.pScreen->format,
			gpSprPal3Bytes[(i * 3)],
			gpSprPal3Bytes[(i * 3) + 1],
			gpSprPal3Bytes[(i * 3) + 2]);

}

#if SPR_SAVE == 1
// R�cup�ration des sprites d'une planche.
void SprLoadBMP(char *pFilename)
{
	SDL_Surface	*pPlanche;
	u32	nNbSprPlanche = 0;
//	u16	*pRemapRGB;		// Table pour remapper les index en couleurs 16 bits.
	u32	ix, iy;
	u8	nBkgClr;		// N� de la couleur de fond de la planche.

	#if	SPRPAL_SUB_ON == 1
	u8	nClrMax = 0;	// Couleur max de la planche pour nb de palettes de x couleurs � sauver.
	#else
	u8	nClrMax = 255;	// Couleur max de la planche pour nb de palettes de x couleurs � sauver.
	#endif

	// Lecture du BMP.
	pPlanche = SDL_LoadBMP(pFilename);
	if (pPlanche == NULL) {
		fprintf(stderr, "Couldn't load picture: %s\n", SDL_GetError());
		exit(1);
	}
	//printf("Load ok!\n");

/*
	// Cr�ation de la table de remappage.
	pRemapRGB = SprRemapPalAlloc(1);
	for (ix = 0; ix < 256; ix++)
	{
		pRemapRGB[ix] = SDL_MapRGB(gVar.pScreen->format,
			pPlanche->format->palette->colors[ix].r,
			pPlanche->format->palette->colors[ix].g,
			pPlanche->format->palette->colors[ix].b);
	}
	pRemapRGB[0] = 0;	// Couleur 0 � 0, car utilis�e dans l'affichage avec un OR.
*/

	// On parcourt la planche pour en extraire les sprites.
	u8	*pPix = (u8 *)pPlanche->pixels;
	nBkgClr = *pPix;		// N� de la couleur de fond de la planche.
//printf("bkg clr idx=%d\n", nBkgClr);
	#if SPRGRAB_DISPLAY_INFO == 1
	printf("w = %d / h = %d\n", pPlanche->w, pPlanche->h);
	#endif

	for (iy = 0; iy < (u32)pPlanche->h; iy++)
	{
		for (ix = 0; ix < (u32)pPlanche->w; ix++)
		{
			// On tombe sur un sprite ?
			if (*(pPix + (iy * pPlanche->pitch) + ix) == 0)
			{
				// On a encore de la place ?
				if (gnSprNbSprites >= gnSprNbSprDefMax) SprDefRealloc();

				#if SPRGRAB_DISPLAY_INFO == 1
				printf("> Sprite at (%d, %d).\n", (int)ix, (int)iy);
				#endif

				u32	LgExt, HtExt;
				u32	PtRefX, PtRefY;		// Pts de ref.
				u32	ii, ij, ik;

				// Recherche des largeurs ext�rieures (cadre de 1 pixel). + Pts de ref (encoches en haut et sur le c�t� gauche).
				PtRefX = 0;
				LgExt = 1;
				ii = ix + 1;
				while (*(pPix + (iy * pPlanche->pitch) + ii) == 0 || *(pPix + (iy * pPlanche->pitch) + ii + 1) == 0)
				{
					if (*(pPix + (iy * pPlanche->pitch) + ii) != 0) PtRefX = LgExt - 1;
					ii++;
					LgExt++;
				}

				PtRefY = 0;
				HtExt = 1;
				ii = iy + 1;
				while(*(pPix + (ii * pPlanche->pitch) + ix) == 0 || *(pPix + ((ii + 1) * pPlanche->pitch) + ix) == 0)
				{
					if (*(pPix + (ii * pPlanche->pitch) + ix) != 0) PtRefY = HtExt - 1;
					ii++;
					HtExt++;
				}
				#if SPRGRAB_DISPLAY_INFO == 1
				printf("lg ext = %d / ht ext = %d / ref point (%d, %d).\n", (int)LgExt, (int)HtExt, (int)PtRefX, (int)PtRefY);
				#endif

				// Stockage des valeurs.
				gpSprDef[gnSprNbSprites].nPtRefX = PtRefX;
				gpSprDef[gnSprNbSprites].nPtRefY = PtRefY;
				gpSprDef[gnSprNbSprites].nLg = LgExt - 2;
				gpSprDef[gnSprNbSprites].nHt = HtExt - 2;

				// Realloc buffer.
				u8	*pSpr8Gfx;
				u32	nSprBufAddSz;

				nSprBufAddSz = gpSprDef[gnSprNbSprites].nLg * gpSprDef[gnSprNbSprites].nHt;	// Taille spr en 8 bits.
				while (gnSprBufSz + nSprBufAddSz >= gnSprBufAllocSz) SprBufRealloc();	// Realloc quand n�cessaire.

				// On garde les index dans les ptrs (union) pour r�affectation finale APRES lecture de tous les sprites (avec le realloc, le bloc peut bouger en m�moire).
				gpSprDef[gnSprNbSprites].nGfx8Offset = gnSprBufSz;
//				gpSprDef[gnSprNbSprites].nRemapPalNo = gnSprRemapPalettesNb - 1;	// N� de la palette de remappage.
				gpSprDef[gnSprNbSprites].nRemapPalNo = gnSprRemapPalettesNb;	// N� de la palette de remappage.

				pSpr8Gfx = gpSprBuf + gpSprDef[gnSprNbSprites].nGfx8Offset;
				// Sz.
				gnSprBufSz += nSprBufAddSz;

				// RAZ zones.
				for (ik = 0; ik < SPRRECT_MAX_ZONES; ik++)
				{
					gpSprDef[gnSprNbSprites].pRect[ik].nType = e_SprRect_NDef;
				}

				// R�cup�ration du sprite.
				ik = 0;
				for (ij = 0; ij < HtExt - 2; ij++)
				{
					for (ii = 0; ii < LgExt - 2; ii++)
					{
						pSpr8Gfx[ik] = *(pPix + ((iy + ij + 1) * pPlanche->pitch) + (ix + ii + 1));	// Index de la couleur.
						#if	SPRPAL_SUB_ON == 1
						if (pSpr8Gfx[ik] > nClrMax) nClrMax = pSpr8Gfx[ik];
						#endif
						ik++;
					}
				}

				// Effacement du sprite dans la planche originale.
				for (ij = 0; ij < HtExt; ij++)
				{
					for (ii = 0; ii < LgExt; ii++)
					{
						*(pPix + ((iy + ij) * pPlanche->pitch) + (ix + ii)) = nBkgClr;
					}
				}

				// Termin�.
				nNbSprPlanche++;
				gnSprNbSprites++;

			}

		}
	}

	// Cr�ation de la table de remappage.
	u32	nNbPal;
	#if	SPRPAL_SUB_ON == 1
	nNbPal = (nClrMax / SPR_PAL_SZ) + 1;
//printf("Color max: %d / Nb of palettes: %d.\n", (int)nClrMax, (int)nNbPal);
	#else
	nNbPal = 1;
	#endif
/*
	pRemapRGB = SprRemapPalAlloc(nNbPal);
	for (ix = 1; ix <= nClrMax; ix++)
	{
		pRemapRGB[ix] = SDL_MapRGB(gVar.pScreen->format,
			pPlanche->format->palette->colors[ix].r,
			pPlanche->format->palette->colors[ix].g,
			pPlanche->format->palette->colors[ix].b);
	}
	pRemapRGB[0] = 0;	// Couleur 0 � 0, car utilis�e dans l'affichage avec un OR.
*/
	// Stockage simple.
	u8	*pRGB3;
	pRGB3 = SprPal3BytesAlloc(nNbPal);
	for (ix = 1; ix <= nClrMax; ix++)
	{
		pRGB3[(ix * 3)] =     pPlanche->format->palette->colors[ix].r;
		pRGB3[(ix * 3) + 1] = pPlanche->format->palette->colors[ix].g;
		pRGB3[(ix * 3) + 2] = pPlanche->format->palette->colors[ix].b;
	}
	pRGB3[0] = pRGB3[1] = pRGB3[2] = 0;	// Couleur 0 � 0, car utilis�e dans l'affichage avec un OR.

	printf(">\nTotal sprites in '%s': %d.\n", pFilename, (int)nNbSprPlanche);
	printf("Total sprites: %d.\n>\n", (int)gnSprNbSprites);

	// On lib�re la surface.
	SDL_FreeSurface(pPlanche);

}

// Lecture d'une planche de sprites en PSD.
void SprLoadPSD(char *pFilename)
{
	struct SPSDPicture	*pPlanche;
	u32	nNbSprPlanche = 0;
//	u16	*pRemapRGB;		// Table pour remapper les index en couleurs 16 bits.
	s32	ix, iy;
	u8	nBkgClr;		// N� de la couleur de fond de la planche.

	#if	SPRPAL_SUB_ON == 1
	u8	nClrMax = 0;	// Couleur max de la planche pour nb de palettes de x couleurs � sauver.
	#else
	u8	nClrMax = 255;	// Couleur max de la planche pour nb de palettes de x couleurs � sauver.
	#endif

	// Lecture de la planche.
	printf("Loading PSD '%s'.\n", pFilename);
	pPlanche = PSDLoad(pFilename);
	if (pPlanche == NULL)
	{
		fprintf(stderr, "Error while loading PSD file '%s'.\n", pFilename);
		exit(1);
	}
	//printf("Load ok!\n");

/*
	// Cr�ation de la table de remappage.
	pRemapRGB = SprRemapPalAlloc(1);
	for (ix = 0; ix < 256; ix++)
	{
		pRemapRGB[ix] = SDL_MapRGB(gVar.pScreen->format,
			pPlanche->pColors[ix].r,
			pPlanche->pColors[ix].g,
			pPlanche->pColors[ix].b);
	}
	pRemapRGB[0] = 0;	// Couleur 0 � 0, car utilis�e dans l'affichage avec un OR.
*/

	// On parcourt la planche pour en extraire les sprites.
	u8	*pPix = pPlanche->pPlanes;
	nBkgClr = *pPix;		// N� de la couleur de fond de la planche.
//printf("bkg clr idx=%d\n", nBkgClr);
	#if SPRGRAB_DISPLAY_INFO == 1
	printf("w = %d / h = %d\n", (int)pPlanche->nWidth, (int)pPlanche->nHeight);
	#endif

	for (iy = 0; iy < pPlanche->nHeight; iy++)
	{
		for (ix = 0; ix < pPlanche->nWidth; ix++)
		{
			// On tombe sur un sprite ?
			if (*(pPix + (iy * pPlanche->nWidth) + ix) != nBkgClr)
			{
				// On a encore de la place ?
				if (gnSprNbSprites >= gnSprNbSprDefMax) SprDefRealloc();

				#if SPRGRAB_DISPLAY_INFO == 1
				printf("> Sprite at (%d, %d).\n", (int)ix, (int)iy);
				#endif

				u32	nLgExt, nHtExt;
				s32	ii, ij, ik, ip;

				// Recherche des largeurs ext�rieures.
				nLgExt = 0;
				ii = ix;
				while(*(pPix + (iy * pPlanche->nWidth) + ii) != nBkgClr)
				{
					ii++;
					nLgExt++;
				}
				nHtExt = 0;
				ii = iy;
				while(*(pPix + (ii * pPlanche->nWidth) + ix) != nBkgClr)
				{
					ii++;
					nHtExt++;
				}
				#if SPRGRAB_DISPLAY_INFO == 1
				printf("lg ext = %d / ht ext = %d\n", (int)nLgExt, (int)nHtExt);
				#endif

				// Taille ok ? (inutile ? (le cadre n'est plus obligatoire))
				if (nLgExt < 3 || nHtExt < 3)
				{
					printf("Pic '%s' sprite #%d : Wrong box size (%d,%d). Aborted.\n", pFilename, (int)nNbSprPlanche, (int)nLgExt, (int)nHtExt);
					SprRelease();
					exit(1);
				}


				// Recadrage du sprite au plus pr�s (bounding box).
				u32	nLgInt, nHtInt;
				s32	nBBoxX1, nBBoxY1, nBBoxX2, nBBoxY2;
				// Haut.
				for (nBBoxY1 = iy; nBBoxY1 < iy + nHtExt; nBBoxY1++)
				{
					for (ii = 0; ii < nLgExt; ii++) if (*(pPix + (nBBoxY1 * pPlanche->nWidth) + (ix + ii)) != 0) break;
					if (ii < nLgExt) break;
				}
				// Bas.
				for (nBBoxY2 = iy + nHtExt - 1; nBBoxY2 >= iy; nBBoxY2--)
				{
					for (ii = 0; ii < nLgExt; ii++) if (*(pPix + (nBBoxY2 * pPlanche->nWidth) + (ix + ii)) != 0) break;
					if (ii < nLgExt) break;
				}
				// Gauche.
				for (nBBoxX1 = ix; nBBoxX1 < ix + nLgExt; nBBoxX1++)
				{
					for (ii = 0; ii < nHtExt; ii++) if (*(pPix + ((iy + ii) * pPlanche->nWidth) + nBBoxX1) != 0) break;
					if (ii < nHtExt) break;
				}
				// Droite.
				for (nBBoxX2 = ix + nLgExt - 1; nBBoxX2 >= ix; nBBoxX2--)
				{
					for (ii = 0; ii < nHtExt; ii++) if (*(pPix + ((iy + ii) * pPlanche->nWidth) + nBBoxX2) != 0) break;
					if (ii < nHtExt) break;
				}
				#if SPRGRAB_DISPLAY_INFO == 1
				printf("ext: %d,%d - %d,%d > int: %d,%d - %d,%d\n",
					(int)ix, (int)iy, (int)(ix + nLgExt - 1), (int)(iy + nHtExt - 1),
					(int)nBBoxX1, (int)nBBoxY1, (int)nBBoxX2, (int)nBBoxY2);
				#endif

				// Si jamais on tombe sur un sprite vide, on reprend les mesures externes.
				if (nBBoxX2 <= nBBoxX1)
//				if (nBBoxX2 < nBBoxX1)
				{
					nBBoxX1 = ix;
					nBBoxX2 = ix + nLgExt - 1;
					printf("Pic '%s' sprite #%d: Empty sprite?\n", pFilename, (int)nNbSprPlanche);
				}
				if (nBBoxY2 <= nBBoxY1)	// == => sprite de 1 pixel.
//				if (nBBoxY2 < nBBoxY1)	// le < au lieu du <= fonctionne � la capture, mais �a pose un pb � l'affichage.
				{
					nBBoxY1 = iy;
					nBBoxY2 = iy + nHtExt - 1;
					printf("Pic '%s' sprite #%d: Empty sprite?\n", pFilename, (int)nNbSprPlanche);
				}

				nLgInt = nBBoxX2 - nBBoxX1 + 1;
				nHtInt = nBBoxY2 - nBBoxY1 + 1;
//printf("lgint = %d / htint = %d\n", nLgInt, nHtInt);


				//... Ins�rer ici le parcours de l'image si jamais il faut faire le stockage sur 4 bits.


				// !!! On ne capture que la bounding box !!!
				// !!! Par contre, on recherche dans les couches alpha sur la totalit� du rectangle ext !!!


				// Stockage des valeurs.
				gpSprDef[gnSprNbSprites].nPtRefX = 0;
				gpSprDef[gnSprNbSprites].nPtRefY = 0;
				gpSprDef[gnSprNbSprites].nLg = nLgInt;
				gpSprDef[gnSprNbSprites].nHt = nHtInt;

				// Realloc buffer.
				u8	*pSpr8Gfx;
				u32	nSprBufAddSz;// = gpSprDef[gnSprNbSprites].nLg * gpSprDef[gnSprNbSprites].nHt * 2 * sizeof(u16);

				nSprBufAddSz = gpSprDef[gnSprNbSprites].nLg * gpSprDef[gnSprNbSprites].nHt;	// Taille spr en 8 bits.
				while (gnSprBufSz + nSprBufAddSz >= gnSprBufAllocSz) SprBufRealloc();	// Realloc quand n�cessaire.

				// On garde les index dans les ptrs (union) pour r�affectation finale APRES lecture de tous les sprites (avec le realloc, le bloc peut bouger en m�moire).
				gpSprDef[gnSprNbSprites].nGfx8Offset = gnSprBufSz;
//				gpSprDef[gnSprNbSprites].nRemapPalNo = gnSprRemapPalettesNb - 1;	// N� de la palette de remappage.
				gpSprDef[gnSprNbSprites].nRemapPalNo = gnSprRemapPalettesNb;	// N� de la palette de remappage.

				pSpr8Gfx = gpSprBuf + gpSprDef[gnSprNbSprites].nGfx8Offset;
				// Sz.
				gnSprBufSz += nSprBufAddSz;

				// RAZ zones.
				for (ip = 0; ip < SPRRECT_MAX_ZONES; ip++)
				{
					gpSprDef[gnSprNbSprites].pRect[ip].nType = e_SprRect_NDef;
				}

				// R�cup�ration du sprite.
				ik = 0;
				for (ij = 0; ij < nHtInt; ij++)
				{
					for (ii = 0; ii < nLgInt; ii++)
					{
						pSpr8Gfx[ik] = *(pPix + ((nBBoxY1 + ij) * pPlanche->nWidth) + (nBBoxX1 + ii));	// Index de la couleur.
						#if	SPRPAL_SUB_ON == 1
						if (pSpr8Gfx[ik] > nClrMax) nClrMax = pSpr8Gfx[ik];
						#endif
						ik++;
					}
				}

				// Boucle dans les plans alpha pour points, rectangles... (O = vide / 255 = plein).
				for (ip = 1; ip < pPlanche->nNbPlanes && ip <= SPRRECT_MAX_ZONES; ip++)
				{
//printf("plane %d\n", (int)ip);
					for (ij = 0; ij < nHtExt; ij++)
					{
						for (ii = 0; ii < nLgExt; ii++)
						{
//printf("%d ", *(pPix + ((iy + ij + 1 + (ip * pPlanche->nHeight)) * pPlanche->nWidth) + (ix + ii + 1)) );

							// Un pixel ?
							if (*(pPix + ((iy + ij + (ip * pPlanche->nHeight)) * pPlanche->nWidth) + (ix + ii)) )
							{
								s32	rii, rij;
								gpSprDef[gnSprNbSprites].pRect[ip - 1].nX1 = ii + (ix - nBBoxX1);
								gpSprDef[gnSprNbSprites].pRect[ip - 1].nY1 = ij + (iy - nBBoxY1);
								// Parcours sur le X.
								rii = ii;
								rij = ij;
								while (*(pPix + ((iy + rij + (ip * pPlanche->nHeight)) * pPlanche->nWidth) + (ix + rii)) && rii < nLgExt) rii++;
								gpSprDef[gnSprNbSprites].pRect[ip - 1].nX2 = rii - 1 + (ix - nBBoxX1);
								// Parcours sur le Y.
								rii = ii;
								rij = ij;
								while (*(pPix + ((iy + rij + (ip * pPlanche->nHeight)) * pPlanche->nWidth) + (ix + rii)) && rij < nHtExt) rij++;
								gpSprDef[gnSprNbSprites].pRect[ip - 1].nY2 = rij - 1 + (iy - nBBoxY1);
								// Pt ou rect ?
								gpSprDef[gnSprNbSprites].pRect[ip - 1].nType =
								(gpSprDef[gnSprNbSprites].pRect[ip - 1].nX1 == gpSprDef[gnSprNbSprites].pRect[ip - 1].nX2 &&
								gpSprDef[gnSprNbSprites].pRect[ip - 1].nY1 == gpSprDef[gnSprNbSprites].pRect[ip - 1].nY2 ? e_SprRect_Point : e_SprRect_Rect);

//printf("Type=%d / x1=%d / y1=%d / x2=%d / y2=%d\n",
//gpSprDef[gnSprNbSprites].pRect[ip - 1].nType,
//gpSprDef[gnSprNbSprites].pRect[ip - 1].nX1, pSpr[gnSprNbSprites].pRect[ip - 1].nY1,
//gpSprDef[gnSprNbSprites].pRect[ip - 1].nX2, pSpr[gnSprNbSprites].pRect[ip - 1].nY2);

								// Force la fin des boucles for.
								ii = nLgExt;
								ij = nHtExt;
							} // if pixel
						} // for ii
//printf("\n");
					} // for ij
				} // for couche
				// On prend le point de la premi�re couche alpha comme point de r�f�rence.
				if (gpSprDef[gnSprNbSprites].pRect[0].nType == e_SprRect_Point)
				{
					gpSprDef[gnSprNbSprites].nPtRefX = gpSprDef[gnSprNbSprites].pRect[0].nX1;
					gpSprDef[gnSprNbSprites].nPtRefY = gpSprDef[gnSprNbSprites].pRect[0].nY1;
					#if SPRGRAB_DISPLAY_INFO == 1
					printf("Ref point - x=%d / y=%d\n", (int)gpSprDef[gnSprNbSprites].nPtRefX, (int)gpSprDef[gnSprNbSprites].nPtRefY);
					#endif
				}
				else
				{
					printf("Picture '%s' sprite #%d: No ref point found!\n", pFilename, (int)nNbSprPlanche);
				}
				// On fait une passe pour d�caler tout par rapport au point de ref (Sauf la premi�re couche alpha, �videment).
				for (ip = 1; ip < SPRRECT_MAX_ZONES; ip++)
				{
					gpSprDef[gnSprNbSprites].pRect[ip].nX1 -= gpSprDef[gnSprNbSprites].nPtRefX;
					gpSprDef[gnSprNbSprites].pRect[ip].nY1 -= gpSprDef[gnSprNbSprites].nPtRefY;
					gpSprDef[gnSprNbSprites].pRect[ip].nX2 -= gpSprDef[gnSprNbSprites].nPtRefX;
					gpSprDef[gnSprNbSprites].pRect[ip].nY2 -= gpSprDef[gnSprNbSprites].nPtRefY;
					#if SPRGRAB_DISPLAY_INFO == 1
					if (gpSprDef[gnSprNbSprites].pRect[ip].nType == e_SprRect_NDef)
						printf("alpha #%d - Not defined.\n", (int)ip);
					else
						printf("alpha #%d - Type=%d / x1=%d / y1=%d / x2=%d / y2=%d\n",
							(int)ip, (int)gpSprDef[gnSprNbSprites].pRect[ip].nType,
							(int)gpSprDef[gnSprNbSprites].pRect[ip].nX1, (int)gpSprDef[gnSprNbSprites].pRect[ip].nY1,
							(int)gpSprDef[gnSprNbSprites].pRect[ip].nX2, (int)gpSprDef[gnSprNbSprites].pRect[ip].nY2);
					#endif
				}

				// Effacement du sprite dans la planche originale.
				for (ij = 0; ij < nHtExt; ij++)
				{
					for (ii = 0; ii < nLgExt; ii++)
					{
						*(pPix + ((iy + ij) * pPlanche->nWidth) + (ix + ii)) = nBkgClr;
					}
				}

				// Termin�.
				nNbSprPlanche++;
				gnSprNbSprites++;

			}

		}
	}


	// Cr�ation de la table de remappage.
	u32	nNbPal;
	#if	SPRPAL_SUB_ON == 1
	nNbPal = (nClrMax / SPR_PAL_SZ) + 1;
//printf("Color max: %d / Nb of palettes: %d.\n", (int)nClrMax, (int)nNbPal);
	#else
	nNbPal = 1;
	#endif
/*
	pRemapRGB = SprRemapPalAlloc(nNbPal);
	for (ix = 1; ix <= nClrMax; ix++)
	{
		pRemapRGB[ix] = SDL_MapRGB(gVar.pScreen->format,
			pPlanche->pColors[ix].r,
			pPlanche->pColors[ix].g,
			pPlanche->pColors[ix].b);
	}
	pRemapRGB[0] = 0;	// Couleur 0 � 0, car utilis�e dans l'affichage avec un OR.
*/
	// Stockage simple.
	u8	*pRGB3;
	pRGB3 = SprPal3BytesAlloc(nNbPal);
	for (ix = 1; ix <= nClrMax; ix++)
	{
		pRGB3[(ix * 3)] =     pPlanche->pColors[ix].r;
		pRGB3[(ix * 3) + 1] = pPlanche->pColors[ix].g;
		pRGB3[(ix * 3) + 2] = pPlanche->pColors[ix].b;
	}
	pRGB3[0] = pRGB3[1] = pRGB3[2] = 0;	// Couleur 0 � 0, car utilis�e dans l'affichage avec un OR.

	printf(">\nTotal sprites in '%s': %d.\n", pFilename, (int)nNbSprPlanche);
	printf("Total sprites: %d.\n>\n", (int)gnSprNbSprites);


	// Free.
	free(pPlanche->pPlanes);
	free(pPlanche);
}


// Lecture des sprites.
void SprLoadGfx(char *pFilename)//, SDL_Color *pSprPal, u32 nPalIdx)
{
	// On regarde l'extension du fichier.
	if (stricmp(pFilename + strlen(pFilename) - 3, "bmp") == 0)
	{
		// Extraction des sprites depuis un BMP.
		SprLoadBMP(pFilename);//pSprPal, nPalIdx);
	}
	else
	if (stricmp(pFilename + strlen(pFilename) - 3, "psd") == 0)
	{
		// Extraction des sprites depuis un PSD.
		SprLoadPSD(pFilename);//, pSprPal, nPalIdx);
	}
	else
	{
		printf("Unrecognised file format: %s\n", pFilename);
	}

}
#endif

// Renvoie un ptr sur un descripteur de sprite.
struct SSprite *SprGetDesc(u32 nSprNo)
{
	return (&gpSprDef[nSprNo & ~(SPR_Flip_X | SPR_Flip_Y | SPR_Flag_HitPal)]);
}



// R�cup�re des pointeurs sur l'image du sprite et son masque.
// L'image est d�pack�e (8 bits > 16 bits) et le masque g�n�r�.
void SprGetGfxMskPtr(u32 nSprFlags, u16 **ppGfx, u16 **ppMsk, struct SSprite *pSprDesc, struct SSprStockage *pSprSto)
{
	s32	i, j, nSz;
	u16	*pDstG, *pDstM, *pPal;
	u8	*pSrc8;

	nSz = pSprDesc->nLg * pSprDesc->nHt;

	#if CACHE_ON == 1
	// Cache pas pris en compte pour les rotations/zoom.
	if (pSprSto->pFct != NULL)
	{
		((pRZFctRender)pSprSto->pFct)();	// Appelle le rendu du zoom ou du rotozoom.
		*ppGfx = pDstG = gpSprFlipBuf;
		*ppMsk = pDstM = gpSprFlipBuf + nSz;
	}
	else
	{
		// Avec cache.
		i = CacheGetMem(nSprFlags, nSz, ppGfx);
		*ppMsk = *ppGfx + nSz;
		if (i == e_Cache_Hit) return;
		pDstG = *ppGfx;
		pDstM = *ppMsk;
	}
	#else
	// Sans cache
	if (pSprSto->pFct != NULL)
	{
		((pRZFctRender)pSprSto->pFct)();	// Appelle le rendu du zoom ou du rotozoom.
	}
	*ppGfx = pDstG = gpSprFlipBuf;
	*ppMsk = pDstM = gpSprFlipBuf + nSz;
	//*** Sans cache, on doit pouvoir se passer des param�tres ppGfx et ppMsk.
	#endif


	pPal = SprRemapPalGet(pSprDesc->nRemapPalNo);//gpSprDef[nSprNo].nRemapPalNo);	// Palette de remappage du sprite.

	// "Depack" du sprite (8 => 16 bits).
	if ((nSprFlags & (SPR_Flip_X | SPR_Flip_Y)) == 0)
	{
		// Pas de flips.
		pSrc8 = pSprDesc->pGfx8;
		i = 0;
		for (i = 0; i < nSz; i++)
		{
			*pDstG++ = *(pPal + *pSrc8);
			*pDstM++ = (*(pSrc8++) ? 0 : 0xFFFF);
		}
	}
	else if ((nSprFlags & (SPR_Flip_X | SPR_Flip_Y)) == (SPR_Flip_X | SPR_Flip_Y))
	{
		// Flip x et y.
		pSrc8 = pSprDesc->pGfx8 + nSz - 1;
		for (i = 0; i < nSz; i++)
		{
			*pDstG++ = *(pPal + *pSrc8);
			*pDstM++ = (*(pSrc8--) ? 0 : 0xFFFF);
		}
	}
	else if (nSprFlags & SPR_Flip_Y)
	{
		// Flip y.
		for (j = pSprDesc->nHt - 1; j >= 0; j--)
		{
			pSrc8 = pSprDesc->pGfx8 + (j * pSprDesc->nLg);
			for (i = 0; i < pSprDesc->nLg; i++)
			{
				*pDstG++ = *(pPal + *pSrc8);
				*pDstM++ = (*(pSrc8++) ? 0 : 0xFFFF);
			}
		}
	}
	else
	{
		// Flip x.
		for (j = 0; j < pSprDesc->nHt; j++)
		{
			pSrc8 = pSprDesc->pGfx8 + ((j + 1) * pSprDesc->nLg) - 1;
			for (i = 0; i < pSprDesc->nLg; i++)
			{
				*pDstG++ = *(pPal + *pSrc8);
				*pDstM++ = (*(pSrc8--) ? 0 : 0xFFFF);
			}
		}
	}

}

// Affichage d'un sprite.
// Avec �cran lock�.
void SprDisplayLock(struct SSprStockage *pSprSto)
{
	s32	nXMin, nXMax, nYMin, nYMax;
	s32	nPtRefX, nPtRefY;
	s32	nSprXMin, nSprXMax, nSprYMin, nSprYMax;
	s32	diff;
	u16	*pScr;
	struct SSprite *pSprDesc;

	u32	nSprFlags = pSprSto->nSprNo;		// Pour conserver les flags.

	// Descripteur de sprite.
	if (pSprSto->pFct == NULL)
	{
		// Sprite standard.
		pSprDesc = &gpSprDef[nSprFlags & ~(SPR_Flag_HitPal | SPR_Flip_X | SPR_Flip_Y)];
	}
	else
	{
		// Sprite roto/zoom�, appel de la fonction de pr�-rendu qui va bien.
		pSprDesc = ((pRZFctPreRender)pSprSto->pFct)(nSprFlags, pSprSto->nZoomX, pSprSto->nZoomY, &pSprSto->pFct);
		if (pSprDesc == NULL) return;	// Il y a eu un pb, abort.
	}

	// Point de ref.
	nPtRefX = pSprDesc->nPtRefX;
	nPtRefY = pSprDesc->nPtRefY;
	// D�calage pt de ref selon les flags (flip x : refX = nLg - refX).
//	if (nSprFlags & SPR_Flip_X) nPtRefX = pSprDesc->nLg - nPtRefX;
//	if (nSprFlags & SPR_Flip_Y) nPtRefY = pSprDesc->nHt - nPtRefY;
	if (nSprFlags & SPR_Flip_X) nPtRefX = (pSprDesc->nLg - 1) - nPtRefX;
	if (nSprFlags & SPR_Flip_Y) nPtRefY = (pSprDesc->nHt - 1) - nPtRefY;

	// Pr�paration du trac�.
	nXMin = pSprSto->nPosX - nPtRefX;
	nXMax = nXMin + pSprDesc->nLg - 1;
	nYMin = pSprSto->nPosY - nPtRefY;
	nYMax = nYMin + pSprDesc->nHt - 1;

	nSprXMin = 0;
	nSprXMax = pSprDesc->nLg - 1;
	nSprYMin = 0;
	nSprYMax = pSprDesc->nHt - 1;

	// Clips.
	if (nXMin < 0)	//aaa0
	{
		diff = 0 - nXMin;	//aaa0
		nSprXMin += diff;
	}
	if (nXMax > SCR_Width - 1)
	{
		diff = nXMax - (SCR_Width - 1);
		nSprXMax -= diff;
	}
	// Sprite compl�tement en dehors ?
//	if (nSprXMin - nSprXMax >= 0) return;	//< bug
	if (nSprXMin - nSprXMax > 0) return;
	//
	if (nYMin < 0)	//aaa0
	{
		diff = 0 - nYMin;	//aaa0
		nSprYMin += diff;
	}
	if (nYMax > SCR_Height - 1)
	{
		diff = nYMax - (SCR_Height - 1);
		nSprYMax -= diff;
	}
	// Sprite compl�tement en dehors ?
//	if (nSprYMin - nSprYMax >= 0) return;	//< bug
	if (nSprYMin - nSprYMax > 0) return;


	s32	ix, iy;
	u32	b4, /*b1,*/ b4b, b1b;
	u16	*pGfx, *pMsk;
//	u32	nScrLg = gVar.pScreen->pitch / sizeof(u16);
	s32	nScrLg = gVar.pScreen->pitch / sizeof(u16);	// Bugfix 11/10/2012. u32 > s32, car unsigned * signed = unsigned. Et il faut le sign extend en 64 bits !

	SprGetGfxMskPtr(nSprFlags, &pGfx, &pMsk, pSprDesc, pSprSto);

	b1b = nSprXMax - nSprXMin + 1;
//8	b4b = b1b >> 2;		// Nb de quads.
//8	b1b &= 3;			// Nb d'octets restants ensuite.
	b4b = b1b >> 1;		// Nb de quads.
	b1b &= 1;			// Nb de words restants ensuite.
	pScr = (u16 *)gVar.pScreen->pixels;
//l	pScr += ((nYMin + nSprYMin) * SCR_Width) + nXMin;
	pScr += ((nYMin + nSprYMin) * nScrLg) + nXMin;
	pMsk += (nSprYMin * pSprDesc->nLg);
	pGfx += (nSprYMin * pSprDesc->nLg);

	if (nSprFlags & SPR_Flag_HitPal)
	{
		// Affichage sprite rougi pour le Hit.

//u32	tst = (gVar.pScreen->format->Rmask << 16) | gVar.pScreen->format->Rmask;
//u32	tst2 = ((gVar.pScreen->format->Gmask >> 1) & gVar.pScreen->format->Gmask) | ((gVar.pScreen->format->Bmask >> 1) & gVar.pScreen->format->Bmask);
//tst2 |= tst2 << 16;

//u32	tst3 = (gVar.pScreen->format->Rmask << 16) | gVar.pScreen->format->Rmask;
u32	tst3 = gVar.pScreen->format->Rmask | ((gVar.pScreen->format->Gmask >> 2) & gVar.pScreen->format->Gmask);
tst3 |= tst3 << 16;

		for (iy = nSprYMin; iy <= nSprYMax; iy++)
		{
			b4 = b4b;

//A			for (ix = nSprXMin; b4 && *(u32 *)(pMsk + ix) == 0xFFFFFFFF; b4--, ix += 2);	// Skippe les premiers pixels vides.
//A			for (; b4; b4--, ix += 2)
//A = tim�, c'est kif kif, peut-�tre m�me pire, c'est dur � dire.
			for (ix = nSprXMin; b4; b4--, ix += 2)
			{
				*(u32 *)(pScr + ix) &= *(u32 *)(pMsk + ix);
//				*(u32 *)(pScr + ix) |= *(u32 *)(pGfx + ix);
				//	*(u32 *)(pScr + ix) |= *(u32 *)(pGfx + ix) |0x0F0F0F0F;	// Pour voir l'affichage.

//*(u32 *)(pScr + ix) |= *(u32 *)(pGfx + ix) & tst;	// juste en gardant le rouge.
//*(u32 *)(pScr + ix) |= ((*(u32 *)(pGfx + ix) >> 1) & tst2) | (*(u32 *)(pGfx + ix) & tst);	// rougi.
*(u32 *)(pScr + ix) |= *(u32 *)(pGfx + ix) | (~*(u32 *)(pMsk + ix) & tst3);	// rouge 2
			}
			if (b1b)	// Un dernier pixel ?
			{
				*(pScr + ix) &= *(pMsk + ix);
//				*(pScr + ix) |= *(pGfx + ix);

//*(pScr + ix) |= *(pGfx + ix) & tst;	// juste en gardant le rouge.
//*(pScr + ix) |= ((*(pGfx + ix) >> 1) & tst2) | (*(pGfx + ix) & tst);	// rougi.
*(pScr + ix) |= *(pGfx + ix) | (~*(pMsk + ix) & tst3);	// rouge 2
			}
//l			pScr += SCR_Width;
			pScr += nScrLg;
			pMsk += pSprDesc->nLg;
			pGfx += pSprDesc->nLg;
		}


	}
	else
	{
		// Affichage normal.

//u32	tst = (gVar.pScreen->format->Rmask << 16) | gVar.pScreen->format->Rmask;
//u32	tst2 = ((gVar.pScreen->format->Gmask >> 1) & gVar.pScreen->format->Gmask) | ((gVar.pScreen->format->Bmask >> 1) & gVar.pScreen->format->Bmask);
//tst2 |= tst2 << 16;
//u32	tst3 = (gVar.pScreen->format->Rmask << 16) | gVar.pScreen->format->Rmask;

		for (iy = nSprYMin; iy <= nSprYMax; iy++)
		{
			b4 = b4b;

//A			for (ix = nSprXMin; b4 && *(u32 *)(pMsk + ix) == 0xFFFFFFFF; b4--, ix += 2);	// Skippe les premiers pixels vides.
//A			for (; b4; b4--, ix += 2)
//A = tim�, c'est kif kif, peut-�tre m�me pire, c'est dur � dire.
			for (ix = nSprXMin; b4; b4--, ix += 2)
			{
				*(u32 *)(pScr + ix) &= *(u32 *)(pMsk + ix);
				*(u32 *)(pScr + ix) |= *(u32 *)(pGfx + ix);
			//	*(u32 *)(pScr + ix) |= *(u32 *)(pGfx + ix) |0x0F0F0F0F;	// Pour voir l'affichage.

//*(u32 *)(pScr + ix) |= *(u32 *)(pGfx + ix) & tst;	// juste en gardant le rouge.
//*(u32 *)(pScr + ix) |= ((*(u32 *)(pGfx + ix) >> 1) & tst2) | (*(u32 *)(pGfx + ix) & tst);	// rougi.
//*(u32 *)(pScr + ix) |= *(u32 *)(pGfx + ix) | (~*(u32 *)(pMsk + ix) & tst3);	// rouge 2
			}
			if (b1b)	// Un dernier pixel ?
			{
				*(pScr + ix) &= *(pMsk + ix);
				*(pScr + ix) |= *(pGfx + ix);

//*(pScr + ix) |= *(pGfx + ix) & tst;	// juste en gardant le rouge.
//*(pScr + ix) |= ((*(pGfx + ix) >> 1) & tst2) | (*(pGfx + ix) & tst);	// rougi.
//*(pScr + ix) |= *(pGfx + ix) | (~*(pMsk + ix) & tst3);	// rouge 2
			}
//l			pScr += SCR_Width;
			pScr += nScrLg;
			pMsk += pSprDesc->nLg;
			pGfx += pSprDesc->nLg;
		}

	}

}


// Macros pour �viter des calls :
#define	SPR_ADD_TO_LIST(POSX, POSY, PRIO, FPTR) \
	if (gnSprSto >= SPR_STO_MAX) { fprintf(stderr, "Sprites: Out of slots!\n"); return; } \
	if ((nSprNo & ~(SPR_Flip_X | SPR_Flip_Y)) == SPR_NoSprite) return; \
	gpSprSto[gnSprSto].nSprNo = nSprNo; \
	gpSprSto[gnSprSto].nPosX = POSX; \
	gpSprSto[gnSprSto].nPosY = POSY; \
	gpSprSto[gnSprSto].nPrio = PRIO; \
	gpSprSto[gnSprSto].pFct = FPTR;
#define	SPR_ADD_TO_LIST_RZ(ZOOMX, ZOOMY) \
	gpSprSto[gnSprSto].nZoomX = ZOOMX; \
	gpSprSto[gnSprSto].nZoomY = ZOOMY;
#define	SPR_ADD_TO_LIST_END \
	gpSprSort[gnSprSto] = &gpSprSto[gnSprSto]; \
	gnSprSto++;

// Inscrit les sprites dans une liste, position relative par rapport � la map.
void SprDisplay(u32 nSprNo, s32 nPosX, s32 nPosY, u16 nPrio)
{
	SPR_ADD_TO_LIST(nPosX - (gScrollPos.nPosX >> 8), nPosY - (gScrollPos.nPosY >> 8), nPrio, NULL)
	SPR_ADD_TO_LIST_END
}
void SprDisplayZoom(u32 nSprNo, s32 nPosX, s32 nPosY, u16 nPrio, u16 nZoomX, u16 nZoomY)
{
	// Pas de zoom ? => On envoie un sprite normal.
	if (nZoomX == 0x0100 && nZoomY == 0x0100)
	{
		SprDisplay(nSprNo, nPosX, nPosY, nPrio);
		return;
	}
	// Stockage.
	SPR_ADD_TO_LIST(nPosX - (gScrollPos.nPosX >> 8), nPosY - (gScrollPos.nPosY >> 8), nPrio, (void *)SprZoom_PreRender)
	SPR_ADD_TO_LIST_RZ(nZoomX, nZoomY)
	SPR_ADD_TO_LIST_END
}
// Sprites en rotation.
void SprDisplayRotoZoom(u32 nSprNo, s32 nPosX, s32 nPosY, u16 nPrio, u8 nAngle, u16 nZoom)
{
	// Pas de rot ? => On envoie un sprite zoom� simple.
	if (nAngle == 0)
	{
		SprDisplayZoom(nSprNo, nPosX, nPosY, nPrio, nZoom, nZoom);
		return;
	}
	// Angle == 128 => On envoie un sprite zoom� simple avec flips X et Y.
	if (nAngle == 128)
	{
		SprDisplayZoom(nSprNo ^ SPR_Flip_X ^ SPR_Flip_Y, nPosX, nPosY, nPrio, nZoom, nZoom);
		return;
	}
	// Stockage.
	SPR_ADD_TO_LIST(nPosX - (gScrollPos.nPosX >> 8), nPosY - (gScrollPos.nPosY >> 8), nPrio, (void *)SprRotoZoom_PreRender)
	SPR_ADD_TO_LIST_RZ(nZoom, nAngle)	// > union sur nAngle et nZoomY.
	SPR_ADD_TO_LIST_END
}

// Pareil, mais position absolue.
void SprDisplayAbsolute(u32 nSprNo, s32 nPosX, s32 nPosY, u16 nPrio)
{
	SPR_ADD_TO_LIST(nPosX, nPosY, nPrio, NULL)
	SPR_ADD_TO_LIST_END
}
void SprDisplayZoomAbsolute(u32 nSprNo, s32 nPosX, s32 nPosY, u16 nPrio, u16 nZoomX, u16 nZoomY)
{
	// Pas de zoom ? => On envoie un sprite normal.
	if (nZoomX == 0x0100 && nZoomY == 0x0100)
	{
		SprDisplayAbsolute(nSprNo, nPosX, nPosY, nPrio);
		return;
	}
	// Stockage.
	SPR_ADD_TO_LIST(nPosX, nPosY, nPrio, (void *)SprZoom_PreRender)
	SPR_ADD_TO_LIST_RZ(nZoomX, nZoomY)
	SPR_ADD_TO_LIST_END
}
// Sprites en rotation.
void SprDisplayRotoZoomAbsolute(u32 nSprNo, s32 nPosX, s32 nPosY, u16 nPrio, u8 nAngle, u16 nZoom)
{
	// Pas de rot ? => On envoie un sprite zoom� simple.
	if (nAngle == 0)
	{
		SprDisplayZoomAbsolute(nSprNo, nPosX, nPosY, nPrio, nZoom, nZoom);
		return;
	}
	// Angle == 128 => On envoie un sprite zoom� simple avec flips X et Y.
	if (nAngle == 128)
	{
		SprDisplayZoomAbsolute(nSprNo ^ SPR_Flip_X ^ SPR_Flip_Y, nPosX, nPosY, nPrio, nZoom, nZoom);
		return;
	}
	// Stockage.
	SPR_ADD_TO_LIST(nPosX, nPosY, nPrio, (void *)SprRotoZoom_PreRender)
	SPR_ADD_TO_LIST_RZ(nZoom, nAngle)	// > union sur nAngle et nZoomY.
	SPR_ADD_TO_LIST_END
}



// La comparaison du qsort.
int qscmp(const void *pEl1, const void *pEl2)
{
	return ((*(struct SSprStockage **)pEl1)->nPrio - (*(struct SSprStockage **)pEl2)->nPrio);
}

extern	u8	gnFrameMissed;

// Trie la liste des sprites et les affiche.
// A appeler une fois par frame.
void SprDisplayAll(void)
{
	u32	i;

//	if (gnSprSto == 0)	// Rien � faire ?
	if (gnSprSto == 0 || gnFrameMissed)	// Rien � faire ?
	{
		gnSprSto = 0;			// RAZ pour le prochain tour (frame miss).
		#if CACHE_ON == 1
		CacheClearOldSpr();		// Nettoyage des sprites trop vieux du cache.
		#endif
		return;
	}

	// Tri sur la priorit�.
	qsort(gpSprSort, gnSprSto, sizeof(struct SSprStockage *), qscmp);

	// Affichage.
	SDL_LockSurface(gVar.pScreen);
	// Sprites normaux.
	for (i = 0; i < gnSprSto; i++)
		SprDisplayLock(gpSprSort[i]);
	SDL_UnlockSurface(gVar.pScreen);

	// RAZ pour le prochain tour.
	gnSprSto = 0;

	#if CACHE_ON == 1
	CacheClearOldSpr();		// Nettoyage des sprites trop vieux du cache.
	#endif

}


u32	gnSprPass2Idx, gnSprPass2Last;

// Idem SprDisplayAll, mais s�par� en 2 appels pour le plan de masquage.
// Premi�re passe.
void SprDisplayAll_Pass1(void)
{
	u32	i;

//	if (gnSprSto == 0)	// Rien � faire ?
	if (gnSprSto == 0 || gnFrameMissed)	// Rien � faire ?
	{
		gnSprSto = 0;			// RAZ pour le prochain tour (frame miss).
		gnSprPass2Last = 0;		// Pour Pass2.
		return;
	}

	// Tri sur la priorit�.
	qsort(gpSprSort, gnSprSto, sizeof(struct SSprStockage *), qscmp);

	// Affichage.
	SDL_LockSurface(gVar.pScreen);
	for (i = 0; i < gnSprSto && gpSprSort[i]->nPrio < 0x100; i++)
		SprDisplayLock(gpSprSort[i]);
	SDL_UnlockSurface(gVar.pScreen);

	// Il en reste ? => Prio > 0x100. On note les valeurs en cours.
	if (i < gnSprSto)
	{
		gnSprPass2Idx = i;
		gnSprPass2Last = gnSprSto;
	}
	else
		gnSprPass2Last = 0;

	// RAZ pour le prochain tour.
	gnSprSto = 0;

}

// Deuxi�me passe pour les sprites affich�s au dessus du plan de masquage.
// A appeler une fois par frame, APRES SprDisplayAll_Pass1 !
void SprDisplayAll_Pass2(void)
{
	u32	i;

	if (gnFrameMissed == 0)
	if (gnSprPass2Last)		// Quelque chose � faire ?
	{
		// Affichage.
		SDL_LockSurface(gVar.pScreen);
		for (i = gnSprPass2Idx; i < gnSprPass2Last; i++)
			SprDisplayLock(gpSprSort[i]);
		SDL_UnlockSurface(gVar.pScreen);
	}

	#if CACHE_ON == 1
	CacheClearOldSpr();		// Nettoyage des sprites trop vieux du cache.
	#endif

}





// Teste une collision entre 2 sprites.
// Out: 1 col, 0 pas col.
u32 SprCheckColBox(u32 nSpr1, s32 nPosX1, s32 nPosY1, u32 nSpr2, s32 nPosX2, s32 nPosY2)
{
	s32	nXMin1, nXMax1, nYMin1, nYMax1;
	s32	nXMin2, nXMax2, nYMin2, nYMax2;
	struct SSprRect sRect1, sRect2;

	if (SprGetRect(nSpr1, e_SprRectZone_RectCol, &sRect1) == 0) return (0);
	if (SprGetRect(nSpr2, e_SprRectZone_RectCol, &sRect2) == 0) return (0);

	nXMin1 = nPosX1 + sRect1.nX1;
	nXMax1 = nPosX1 + sRect1.nX2;
	nYMin1 = nPosY1 + sRect1.nY1;
	nYMax1 = nPosY1 + sRect1.nY2;

	nXMin2 = nPosX2 + sRect2.nX1;
	nXMax2 = nPosX2 + sRect2.nX2;
	nYMin2 = nPosY2 + sRect2.nY1;
	nYMax2 = nPosY2 + sRect2.nY2;

	// Collisions entre les rectangles ?
	if (nXMax1 >= nXMin2 && nXMin1 <= nXMax2 && nYMax1 >= nYMin2 && nYMin1 <= nYMax2)
	{
		return (1);
	}

	return (0);
}


// R�cup�re un rectangle (ou point) pour un sprite. Prise en compte des bits de flip.
// Out : 0 = Pas bon. 1 = Ok, et rectangle copi� dans pRectDst.
u32 SprGetRect(u32 nSprNo, u32 nZone, struct SSprRect *pRectDst)
{
	struct SSprite *pSprDesc;
	s32	nTmp;

	if ((nSprNo & ~(SPR_Flip_X | SPR_Flip_Y)) == SPR_NoSprite) return (0);
	pSprDesc = SprGetDesc(nSprNo);			// < GetDesc fera les masques.
	if (pSprDesc->pRect[nZone].nType == e_SprRect_NDef) return (0);

	memcpy(pRectDst, &pSprDesc->pRect[nZone], sizeof(struct SSprRect));
	switch(pRectDst->nType)
	{
	case e_SprRect_Point:
		if (nSprNo & SPR_Flip_X) pRectDst->nX1 = -pRectDst->nX1;
		if (nSprNo & SPR_Flip_Y) pRectDst->nY1 = -pRectDst->nY1;
		break;

	case e_SprRect_Rect:
		if (nSprNo & SPR_Flip_X) { nTmp = pRectDst->nX1; pRectDst->nX1 = -pRectDst->nX2; pRectDst->nX2 = -nTmp; }
		if (nSprNo & SPR_Flip_Y) { nTmp = pRectDst->nY1; pRectDst->nY1 = -pRectDst->nY2; pRectDst->nY2 = -nTmp; }
		break;
	}

	return (1);
}


