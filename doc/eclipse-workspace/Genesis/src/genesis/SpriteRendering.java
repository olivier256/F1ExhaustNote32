package genesis;

public class SpriteRendering {
	
	private static final int NB_OF_BYTES_PER_TILE = 32;

	private static final int NB_OF_BYTES_PER_TILE_LINE = 4;
	
	private static byte[] vram = new byte[65_536];
	
	private static short[] cram = new short[4*16];
	
	private static int currentRasterLine;
	
	private static SpriteCache[] cache = new SpriteCache[80];

	private static Sprite[] sprites = new Sprite[80];
	
	private static Sprite[] visibleSprites = new Sprite[20];
	
	private static SpriteLine[] spriteLines = new SpriteLine[20];
	
	private static PixelData[] lineBuffer = new PixelData[320];
	
	public static void renderSprites() {
		currentRasterLine = 0;
		while (currentRasterLine < 224) {
			selectVisibleSpritesFromCache();
			buildSpriteLinesFromVisibleSprites();
			buildLineBufferSpriteLines();
			fillLinePixelFromLineBuffer();
			currentRasterLine++;
		}
	}
	
	// Phase 1. Get visible sprites
	public static void selectVisibleSpritesFromCache() {
		int nbOfVisibleSprites = 0;
		int nbOfExaminedSprites = 0;
		int spriteIndex = 0;
		while (nbOfVisibleSprites < 20 && nbOfExaminedSprites < 80) {
			SpriteCache currentSprite = cache[spriteIndex];
			int yMax = currentSprite.getY() + (currentSprite.getHeight() + 1) << 3;
			if (currentSprite.getY() <= currentRasterLine && currentRasterLine <= yMax) {
				visibleSprites[nbOfVisibleSprites] = sprites[spriteIndex];
				nbOfVisibleSprites++;
			}
			nbOfExaminedSprites++;
			spriteIndex = currentSprite.getLinkNumber();
			if (spriteIndex==0) {
				break;
			}
		}
	}
	
	// Phase 2. Get sensible information
	public static void buildSpriteLinesFromVisibleSprites() {
		int index = 0;
		for (Sprite sprite : visibleSprites) {
			int dy = currentRasterLine - sprite.getY();
			int addr = 0;
			if (sprite.getVerticalFlip() == 0) {
				addr = (sprite.getTileNumber() + (dy / 8)) * NB_OF_BYTES_PER_TILE + (dy % 8) * NB_OF_BYTES_PER_TILE_LINE;
			} else {
				addr = (sprite.getTileNumber() + sprite.getHeight() - (dy / 8)) * NB_OF_BYTES_PER_TILE + (8 - (dy % 8)) * NB_OF_BYTES_PER_TILE_LINE;
			}
			SpriteLine spriteLine = new SpriteLine(sprite.toSpriteLine());
			spriteLine.setAddr(addr);
			spriteLines[index] = spriteLine;
			index++;
		}
	}
	
	// Phase 3
	public static void buildLineBufferSpriteLines() {
		for (SpriteLine spriteLine : spriteLines) {
			if (spriteLine.getHorizontalFlip() == 0) {
				int addr = spriteLine.getAddr();
				for (int w = 0; w < spriteLine.getWidth() + 1; w++ ) {
						for (int dx = 0; dx < 8; dx++) {
							int x = spriteLine.getX() + 8*w + dx;
							if (128 <= x && x < 448) {
								// TODO Si dx >= 448, tous les suivants le seront aussi, il vaut mieux sortir
								PixelData pixelData = new PixelData(spriteLine.toPixelData());
								int vramByte = addr + dx/2;
								int nibble = getNibble(vramByte, x);
								pixelData.setPixel(nibble);
								lineBuffer[x] = pixelData;
							}
						}
						addr += spriteLine.getHeight() * 32;
				}
			} else {
				// TODO Horizontal Flip
			}
		}
	}

	private static byte getNibble(int address, int x) {
		byte nibble = 0;
		if (x % 2 == 0) {
			nibble = vram[address & 0x0F];
		} else {
			nibble = vram[address >> 4];
		}
		return nibble;
	}

	// Phase 4
	public static void fillLinePixelFromLineBuffer() {
		for (PixelData b : lineBuffer) {
			if ( b.getPixel() != 0) {	// Transparent
				if ( b.getPriority() == 1) {
					pushPixel(cram[b.getCramAddress()]);
				} else {
					// TODO Priority
				}
			}
		}
	}
	
	public static void pushPixel(short color) {
		System.out.println("output R = $" + String.format("%02X", ((color >> 1) & 0x03)));
		System.out.println("output G = $" + String.format("%02X", ((color >> 5) & 0x03)));
		System.out.println("output B = $" + String.format("%02X", ((color >> 9) & 0x03)));
	}
}
