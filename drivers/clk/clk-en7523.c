// SPDX-License-Identifier: GPL-2.0-only

#include <linux/regmap.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/en7523-clk.h>
#include <linux/clk.h>

#define REG_PCI_CONTROL			0x88
#define   REG_PCI_CONTROL_PERSTOUT	BIT(29)
#define   REG_PCI_CONTROL_PERSTOUT1	BIT(26)
#define   REG_PCI_CONTROL_REFCLK	BIT(22)
#define REG_GSW_CLK_DIV_SEL		0x1b4
#define REG_EMI_CLK_DIV_SEL		0x1b8
#define REG_BUS_CLK_DIV_SEL		0x1bc
#define REG_SPI_CLK_DIV_SEL		0x1c4
#define REG_SPI_CLK_FREQ_SEL		0x1c8
#define REG_NPU_CLK_DIV_SEL		0x1fc
#define REG_CRYPTO_CLKSRC		0x200
#define REG_RESET_CONTROL		0x834
#define   REG_RESET_CONTROL_PCIEHB	BIT(29)
#define   REG_RESET_CONTROL_PCIE1	BIT(27)
#define   REG_RESET_CONTROL_PCIE2	BIT(26)

struct en_clk_desc {
	int id;
	const char *name;
	u32 base_reg;
	u32 base_bits;
	u32 base_shift;
	union {
		const u32 *base_values;
		u32 base_value;
	};
	int n_base_values;

	u32 div_reg;
	u32 div_bits;
	u32 div_shift;
	u32 div_val0;
	u32 div_step;
};

struct en_clk_gate {
	void __iomem *base;
	struct clk_hw hw;
};

static const u32 gsw_base[] = { 400000000, 500000000 };
static const u32 emi_base[] = { 333000000, 400000000 };
static const u32 bus_base[] = { 500000000, 540000000 };
static const u32 slic_base[] = { 100000000, 3125000 };
static const u32 npu_base[] = { 333000000, 400000000, 500000000 };

static const struct en_clk_desc en7523_base_clks[] = {
	{
		.id = EN7523_CLK_GSW,
		.name = "gsw",

		.base_reg = REG_GSW_CLK_DIV_SEL,
		.base_bits = 1,
		.base_shift = 8,
		.base_values = gsw_base,
		.n_base_values = ARRAY_SIZE(gsw_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
	}, {
		.id = EN7523_CLK_EMI,
		.name = "emi",

		.base_reg = REG_EMI_CLK_DIV_SEL,
		.base_bits = 1,
		.base_shift = 8,
		.base_values = emi_base,
		.n_base_values = ARRAY_SIZE(emi_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
	}, {
		.id = EN7523_CLK_BUS,
		.name = "bus",

		.base_reg = REG_BUS_CLK_DIV_SEL,
		.base_bits = 1,
		.base_shift = 8,
		.base_values = bus_base,
		.n_base_values = ARRAY_SIZE(bus_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
	}, {
		.id = EN7523_CLK_SLIC,
		.name = "slic",

		.base_reg = REG_SPI_CLK_FREQ_SEL,
		.base_bits = 1,
		.base_shift = 0,
		.base_values = slic_base,
		.n_base_values = ARRAY_SIZE(slic_base),

		.div_reg = REG_SPI_CLK_DIV_SEL,
		.div_bits = 5,
		.div_shift = 24,
		.div_val0 = 20,
		.div_step = 2,
	}, {
		.id = EN7523_CLK_SPI,
		.name = "spi",

		.base_reg = REG_SPI_CLK_DIV_SEL,

		.base_value = 400000000,

		.div_bits = 5,
		.div_shift = 8,
		.div_val0 = 40,
		.div_step = 2,
	}, {
		.id = EN7523_CLK_NPU,
		.name = "npu",

		.base_reg = REG_NPU_CLK_DIV_SEL,
		.base_bits = 2,
		.base_shift = 8,
		.base_values = npu_base,
		.n_base_values = ARRAY_SIZE(npu_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
	}, {
		.id = EN7523_CLK_CRYPTO,
		.name = "crypto",

		.base_reg = REG_CRYPTO_CLKSRC,
		.base_bits = 1,
		.base_shift = 8,
		.base_values = emi_base,
		.n_base_values = ARRAY_SIZE(emi_base),
	}
};

static const struct of_device_id of_match_clk_en7523[] = {
	{ .compatible = "econet,en7523-scu", },
	{ /* sentinel */ }
};

static u32
en7523_get_base_rate(void __iomem *base, int i)
{
	const struct en_clk_desc *desc = &en7523_base_clks[i];
	u32 val;

	if (!desc->base_bits)
		return desc->base_value;

	val = readl(base + desc->base_reg);
	val >>= desc->base_shift;
	val &= (1 << desc->base_bits) - 1;

	if (val >= desc->n_base_values)
		return 0;

	return desc->base_values[val];
}

static u32
en7523_get_div(void __iomem *base, int i)
{
	const struct en_clk_desc *desc = &en7523_base_clks[i];
	u32 reg, val;

	if (!desc->div_bits)
		return 1;

	reg = desc->div_reg ? desc->div_reg : desc->base_reg;
	val = readl(base + reg);
	val >>= desc->div_shift;
	val &= (1 << desc->div_bits) - 1;

	if (!val && desc->div_val0)
		return desc->div_val0;

	return (val + 1) * desc->div_step;
}

static int en7523_pci_is_enabled(struct clk_hw *hw)
{
	struct en_clk_gate *cg = container_of(hw, struct en_clk_gate, hw);

	return !!(readl(cg->base + REG_PCI_CONTROL) & REG_PCI_CONTROL_REFCLK);
}

static int en7523_pci_enable(struct clk_hw *hw)
{
	struct en_clk_gate *cg = container_of(hw, struct en_clk_gate, hw);
	void *np_base = cg->base;
	u32 val, mask;

	val = readl(np_base + REG_PCI_CONTROL);
	val &= ~(REG_PCI_CONTROL_PERSTOUT1 | REG_PCI_CONTROL_PERSTOUT);
	writel(val, np_base + REG_PCI_CONTROL);
	mdelay(1);

	val |= REG_PCI_CONTROL_REFCLK;
	writel(val, np_base + REG_PCI_CONTROL);

	mdelay(1);

	val = readl(np_base + REG_RESET_CONTROL);
	mask = REG_RESET_CONTROL_PCIE1 | REG_RESET_CONTROL_PCIE2 |
	       REG_RESET_CONTROL_PCIEHB;
	writel(val & ~mask, np_base + REG_RESET_CONTROL);
	mdelay(1);
	writel(val | mask, np_base + REG_RESET_CONTROL);
	mdelay(100);
	writel(val & ~mask, np_base + REG_RESET_CONTROL);
	mdelay(5);

	mask = REG_PCI_CONTROL_PERSTOUT1 | REG_PCI_CONTROL_PERSTOUT;
	val = readl(np_base + REG_PCI_CONTROL);
	writel(val & ~mask, np_base + REG_PCI_CONTROL);
	mdelay(1);
	writel(val | mask, np_base + REG_PCI_CONTROL);
	mdelay(250);

	return 0;
}

static void en7523_pci_disable(struct clk_hw *hw)
{
	struct en_clk_gate *cg = container_of(hw, struct en_clk_gate, hw);
	void *np_base = cg->base;
	u32 val;

	val = readl(np_base + REG_PCI_CONTROL);
	val &= ~REG_PCI_CONTROL_REFCLK;
	writel(val, np_base + REG_PCI_CONTROL);
}

static struct clk *
en7523_register_pcie_clk(struct device *dev, void __iomem *np_base)
{
	static const struct clk_ops pcie_gate_ops = {
		.is_enabled = en7523_pci_is_enabled,
		.enable = en7523_pci_enable,
		.disable = en7523_pci_disable,
	};
	struct clk_init_data init = {
		.name = "pcie",
		.ops = &pcie_gate_ops,
	};
	struct en_clk_gate *cg;
	struct clk *clk;

	cg = devm_kzalloc(dev, sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return NULL;

	cg->base = np_base;
	cg->hw.init = &init;
	en7523_pci_disable(&cg->hw);

	clk = clk_register(NULL, &cg->hw);
	if (IS_ERR(clk))
		clk = NULL;

	return clk;
}

static void
en7523_register_clocks(struct device *dev, struct clk_onecell_data *clk_data,
		       void __iomem *base, void __iomem *np_base)
{
	struct clk *clk;
	u32 rate;
	int i;

	for (i = 0; i < ARRAY_SIZE(en7523_base_clks); i++) {
		const struct en_clk_desc *desc = &en7523_base_clks[i];

		rate = en7523_get_base_rate(base, i);
		rate /= en7523_get_div(base, i);

		clk = clk_register_fixed_rate(NULL, desc->name, NULL, 0, rate);
		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
			       desc->name, PTR_ERR(clk));
			continue;
		}

		clk_data->clks[desc->id] = clk;
	}

	clk = en7523_register_pcie_clk(dev, np_base);
	clk_data->clks[EN7523_CLK_PCIE] = clk;

	clk_data->clk_num = EN7523_NUM_CLOCKS;
}

static int
en7523_clk_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_onecell_data *clk_data;
	void __iomem *base, *np_base;
	int r;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	np_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(base))
		return PTR_ERR(np_base);

	clk_data = devm_kzalloc(&pdev->dev, sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->clks = devm_kcalloc(&pdev->dev, EN7523_NUM_CLOCKS,
				      sizeof(*clk_data->clks), GFP_KERNEL);
	if (!clk_data->clks)
		return -ENOMEM;

	en7523_register_clocks(&pdev->dev, clk_data, base, np_base);

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static struct platform_driver clk_en7523_drv = {
	.probe = en7523_clk_probe,
	.driver = {
		.name = "clk-en7523",
		.of_match_table = of_match_clk_en7523,
	},
};

static int clk_en7523_init(void)
{
	return platform_driver_register(&clk_en7523_drv);
}

arch_initcall(clk_en7523_init);
