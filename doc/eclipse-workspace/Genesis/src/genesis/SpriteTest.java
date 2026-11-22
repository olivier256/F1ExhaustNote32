package genesis;

public class SpriteTest extends Test {
	
	public void testY() {
		Sprite sprite = new Sprite();
		for (int y=0;y<512;y++) {
			sprite.setY(y);
			assert sprite.getY() == y : "testY(). Expected: " + y + " - Actual: " + sprite.getY();
		}
	}
	
	public void testWidth() {
		Sprite sprite = new Sprite();
		for (int width=0;width<4;width++) {
			sprite.setWidth(width);
			int actual = sprite.getWidth();
			assert actual == width : "testWidth(). Expected: " + width + " - Actual: " + actual;
		}
	}
	
	public void testHeight() {
		Sprite sprite = new Sprite();
		for (int height=0;height<4;height++) {
			sprite.setHeight(height);
			int actual = sprite.getHeight();
			assert actual == height : "testHeight(). Expected: " + height + " - Actual: " + actual;
		}
	}
	
	public void testLinkNumber() {
		Sprite sprite = new Sprite();
		for (int linkNumber=0;linkNumber<128;linkNumber++) {
			sprite.setLinkNumber(linkNumber);
			int actual = sprite.getLinkNumber();
			assert actual == linkNumber : "testLinkNumber(). Expected: " + linkNumber + " - Actual: " + actual;
		}
	}
	
	public void testPriority() {
		Sprite sprite = new Sprite();
		sprite.setPriority(0);
		int actual = sprite.getPriority();
		assert actual == 0 : "testPriority(). Expected: 0 - Actual: " + actual;
		sprite.setPriority(1);
		actual = sprite.getPriority();
		assert actual == 1 : "testPriority(). Expected: 1 - Actual: " + actual;
	}
	
	public void testPaletteNumber() {
		Sprite sprite = new Sprite();
		for (int paletteNumber=0;paletteNumber<4;paletteNumber++) {
			sprite.setPaletteNumber(paletteNumber);
			int actual = sprite.getPaletteNumber();
			assert actual == paletteNumber : "testPaletteNumber(). Expected: " + paletteNumber + " - Actual: " + actual;
		}
	}
	
	private void testVerticalFlip() {
		Sprite sprite = new Sprite();
		sprite.setVerticalFlip(0);
		int actual = sprite.getVerticalFlip();
		assert actual == 0 : "testVerticalFlip(). Expected: 0 - Actual: " + actual;
		sprite.setVerticalFlip(1);
		actual = sprite.getVerticalFlip();
		assert actual == 1 : "testVerticalFlip(). Expected: 1 - Actual: " + actual;
	}

	private void testHorizontalFlip() {
		Sprite sprite = new Sprite();
		sprite.setHorizontalFlip(0);
		int actual = sprite.getHorizontalFlip();
		assert actual == 0 : "testHorizontalFlip(). Expected: 0 - Actual: " + actual;
		sprite.setHorizontalFlip(1);
		actual = sprite.getHorizontalFlip();
		assert actual == 1 : "testHorizontalFlip(). Expected: 1 - Actual: " + actual;
	}

	private void testTileNumber() {
		Sprite sprite = new Sprite();
		for (int tileNumber=0;tileNumber<2048;tileNumber++) {
			sprite.setTileNumber(tileNumber);
			int actual = sprite.getTileNumber();
			assert actual == tileNumber : "testTileNumber(). Expected: " + tileNumber + " - Actual: " + actual;
		}
	}

	private void testX() {
		Sprite sprite = new Sprite();
		for (int x=0;x<512;x++) {
			sprite.setX(x);
			int actual = sprite.getX();
			assert actual == x : "testX(). Expected: " + x + " - Actual: " + actual;
		}
	}
	
	private void testToString() {
		Sprite sprite = new Sprite();
		sprite.setY(224);
		sprite.setWidth(4-1);
		sprite.setHeight(3-1);
		sprite.setLinkNumber(7);
		sprite.setPaletteNumber(1);
		sprite.setTileNumber(1251);
		sprite.setX(384);
		String expected = "	DC.W	$00E0,$0E07,$24E3,$0180";
		String actual = sprite.toString();
		assert actual.equals(expected): "Expected: '" + expected + "' - Actual: '" + actual + "'";
	}
	
	private void testToSpriteLine() {
		Sprite sprite = new Sprite();
		for (int priority = 0; priority < 2; priority++) {
			for (int paletteNumber = 0; paletteNumber < 4; paletteNumber++) {
				for (int horizontalFlip = 0; horizontalFlip < 2; horizontalFlip++) {
					for (int width = 0; width < 4; width++) {
						for (int height = 0; height < 4; height++) {
							for (int x = 0; x < 512; x++) {
								sprite.setPriority(priority);
								sprite.setPaletteNumber(paletteNumber);
								sprite.setHorizontalFlip(horizontalFlip);
								sprite.setWidth(width);
								sprite.setHeight(height);
								sprite.setX(x);
								int expected = priority << 31
										| paletteNumber << 29
										| horizontalFlip << 27
										| width << 25
										| height << 23
										| x;
								int actual = sprite.toSpriteLine();
								assert expected == actual : "testToSpriteLine(). Expected: " + String.format("%08X", expected) + " - Actual: " + String.format("%08X", actual);
							}
						}
					}
				}
			}
		}
	}

	public static void main(String[] args) {
		SpriteTest test = new SpriteTest();
		
		test.testY();
		test.testWidth();
		test.testHeight();
		test.testLinkNumber();
		test.testPriority();
		test.testPaletteNumber();
		test.testVerticalFlip();
		test.testHorizontalFlip();
		test.testTileNumber();
		test.testX();
		test.testToString();
		test.testToSpriteLine();
		
		System.out.println("Tests terminés avec succès");
	}

}
