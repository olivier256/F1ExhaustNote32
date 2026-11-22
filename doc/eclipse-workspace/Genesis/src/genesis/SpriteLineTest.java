package genesis;

public class SpriteLineTest extends Test {
	
	public void testPriority() {
		SpriteLine spriteLine = new SpriteLine();
		spriteLine.setPriority(0);
		int actual = spriteLine.getPriority();
		assert actual == 0 : "Expected: 0 - Actual: " + actual;
		spriteLine.setPriority(1);
		actual = spriteLine.getPriority();
		assert actual == 1 : "testPriority(). Expected: 1 - Actual: " + actual;
	}
	
	private void testPaletteNumber() {
		SpriteLine spriteLine = new SpriteLine();
		for (int paletteNumber=0;paletteNumber<4;paletteNumber++) {
			spriteLine.setPaletteNumber(paletteNumber);
			int actual = spriteLine.getPaletteNumber();
			assert actual == paletteNumber : "testPaletteNumber(). Expected: " + paletteNumber + " - Actual: " + actual;
		}
	}

	private void testHorizontalFlip() {
		SpriteLine spriteLine = new SpriteLine();
		spriteLine.setHorizontalFlip(0);
		int actual = spriteLine.getHorizontalFlip();
		assert actual == 0 : "testHorizontalFlip(). Expected: 0 - Actual: " + actual;
		spriteLine.setHorizontalFlip(1);
		actual = spriteLine.getHorizontalFlip();
		assert actual == 1 : "testHorizontalFlip(). Expected: 1 - Actual: " + actual;
	}

	private void testWidth() {
		SpriteLine spriteLine = new SpriteLine();
		for (int width=0;width<4;width++) {
			spriteLine.setWidth(width);
			int actual = spriteLine.getWidth();
			assert actual == width : "testWidth(). Expected: " + width + " - Actual: " + actual;
		}
	}

	private void testHeight() {
		SpriteLine spriteLine = new SpriteLine();
		for (int height = 0; height < 4; height++) {
			spriteLine.setHeight(height);
			int actual = spriteLine.getHeight();
			assert actual == height : "testHeight(). Expected: " + height + " - Actual: " + actual;
		}
	}

	private void testAddr() {
		SpriteLine spriteLine = new SpriteLine();
		for (int addr = 0; addr < 16_384; addr++) {
			spriteLine.setAddr(addr);
			int actual = spriteLine.getAddr();
			assert actual == addr : "testAddr(). Expected: " + addr + " - Actual: " + actual;
		}
	}

	private void testX() {
		SpriteLine spriteLine = new SpriteLine();
		for (int x = 0; x < 512; x++) {
			spriteLine.setX(x);
			int actual = spriteLine.getX();
			assert actual == x : "testX(). Expected: " + x + " - Actual: " + actual;
		}
	}
	
	private void testConstructor() {
		for (int priority = 0; priority < 2; priority++) {
			for (int paletteNumber = 0; paletteNumber < 4; paletteNumber++) {
				for (int horizontalFlip = 0; horizontalFlip < 2; horizontalFlip++) {
					for (int width = 0; width < 4; width++) {
						for (int height = 0; height < 4; height++) {
							for (int addr = 0; addr < 16384; addr++) {
								for (int x = 0; x < 512; x++) {
									int data = priority << 31
											| paletteNumber << 29
											| horizontalFlip << 27
											| width << 25
											| height << 23
											| addr << 9
											| x;
									SpriteLine spriteLine = new SpriteLine(data);
									
									assert spriteLine.getPriority() == priority : "testConstructor(). Priority. Expected: " + priority + " - Actual: " + spriteLine.getPriority();
									assert spriteLine.getPaletteNumber() == paletteNumber : "testConstructor(). PaletteNumber. Expected: " + paletteNumber + " - Actual: " + spriteLine.getPaletteNumber();
									assert spriteLine.getHorizontalFlip() == horizontalFlip : "testConstructor(). Horizontal Flip. Expected: " + horizontalFlip + " - Actual: " + spriteLine.getHorizontalFlip();
									assert spriteLine.getWidth() == width : "testConstructor(). Width. Expected: " + width + " - Actual: " + spriteLine.getWidth();
									assert spriteLine.getHeight() == height : "testConstructor(). Height. Expected: " + height + " - Actual: " + spriteLine.getHeight();
									assert spriteLine.getAddr() == addr : "testConstructor(). Addr. Expected: " + addr + " - Actual: " + spriteLine.getAddr();
									assert spriteLine.getX() == x : "testConstructor(). X. Expected: " + x + " - Actual: " + spriteLine.getX();
								}
							}
						}
					}
				}
			}
		}
	}
	
	private void testToPixelData() {
		SpriteLine spriteLine = new SpriteLine();
		for (int priority = 0; priority < 2; priority++) {
			for (int paletteNumber = 0; paletteNumber < 4; paletteNumber++) {
				spriteLine.setPriority(priority);
				spriteLine.setPaletteNumber(paletteNumber);
				byte expected = (byte) (priority << 6 | paletteNumber << 4);
				assert spriteLine.toPixelData() == expected : "testToPixelData(). Expected: " + expected + " - Actual: " + spriteLine.toPixelData(); 
			}
		}
	}

	public static void main(String[] args) {
		SpriteLineTest test = new SpriteLineTest();
		
		test.testPriority();
		test.testPaletteNumber();
		test.testHorizontalFlip();
		test.testWidth();
		test.testHeight();
		test.testAddr();
		test.testX();
		test.testConstructor();
		test.testToPixelData();
		
		System.out.println("Tests terminés avec succès");
	}

}
