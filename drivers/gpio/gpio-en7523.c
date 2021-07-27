// SPDX-License-Identifier: GPL-2.0-only

#include <linux/gpio/driver.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define ECONET_GPIO_MAX		32

/**
 * econet_gpio_ctrl - EcoNet GPIO driver data
 *
 * @gc: Associated gpio_chip instance.
 * @data: The data register.
 * @dir0: The direction register for the lower 16 pins.
 * @dir1: The direction register for the lower 16 pins.
 * @output: The output enable register.
 */
struct econet_gpio_ctrl {
	struct gpio_chip gc;
	void __iomem *data;
	void __iomem *dir[2];
	void __iomem *output;
};

static struct econet_gpio_ctrl *gc_to_ctrl(struct gpio_chip *gc)
{
	return container_of(gc, struct econet_gpio_ctrl, gc);
}

static int econet_dir_set(struct gpio_chip *gc, unsigned int gpio,
			  int val, int out)
{
	struct econet_gpio_ctrl *ctrl = gc_to_ctrl(gc);
	u32 dir = ioread32(ctrl->dir[gpio / 16]);
	u32 output = ioread32(ctrl->output);
	u32 mask = BIT((gpio % 16) * 2);

	if (out) {
		dir |= mask;
		output |= BIT(gpio);
	} else {
		dir &= ~mask;
		output &= ~BIT(gpio);
	}

	iowrite32(dir, ctrl->dir[gpio / 16]);
	iowrite32(output, ctrl->output);

	if (out)
		gc->set(gc, gpio, val);

	return 0;
}

static int econet_dir_out(struct gpio_chip *gc, unsigned int gpio,
			  int val)
{
	return econet_dir_set(gc, gpio, val, 1);
}

static int econet_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	return econet_dir_set(gc, gpio, 0, 0);
}

static int econet_get_dir(struct gpio_chip *gc, unsigned int gpio)
{
	struct econet_gpio_ctrl *ctrl = gc_to_ctrl(gc);
	u32 dir = ioread32(ctrl->dir[gpio / 16]);
	u32 mask = BIT((gpio % 16) * 2);

	return dir & mask;
}

static const struct of_device_id econet_gpio_of_match[] = {
	{ .compatible = "econet,en7523-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, econet_gpio_of_match);

static int econet_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct econet_gpio_ctrl *ctrl;
	int err;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->data = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctrl->data))
		return PTR_ERR(ctrl->data);

	ctrl->dir[0] = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(ctrl->dir[0]))
		return PTR_ERR(ctrl->dir[0]);

	ctrl->dir[1] = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(ctrl->dir[1]))
		return PTR_ERR(ctrl->dir[1]);

	ctrl->output = devm_platform_ioremap_resource(pdev, 3);
	if (IS_ERR(ctrl->output))
		return PTR_ERR(ctrl->output);

	err = bgpio_init(&ctrl->gc, dev, 4, ctrl->data, NULL,
			 NULL, NULL, NULL, 0);
	if (err) {
		dev_err(dev, "unable to init generic GPIO");
		return err;
	}

	ctrl->gc.ngpio = ECONET_GPIO_MAX;
	ctrl->gc.owner = THIS_MODULE;
	ctrl->gc.direction_output = econet_dir_out;
	ctrl->gc.direction_input = econet_dir_in;
	ctrl->gc.get_direction = econet_get_dir;

	return devm_gpiochip_add_data(dev, &ctrl->gc, ctrl);
}

static struct platform_driver econet_gpio_driver = {
	.driver = {
		.name = "econet-gpio",
		.of_match_table	= econet_gpio_of_match,
	},
	.probe = econet_gpio_probe,
};
module_platform_driver(econet_gpio_driver);

MODULE_DESCRIPTION("EcoNet GPIO support");
MODULE_AUTHOR("John Crispin <john@phrozen.org>");
MODULE_LICENSE("GPL v2");
